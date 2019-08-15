#include "stdafx.h"
#include "Globals.h"
#include "Blam\Enums\Enums.h"
#include "..\..\Modules\Networking\Networking.h"
#include "..\..\Modules\Networking\CustomPackets\CustomPackets.h"

using namespace Blam::Enums;

std::vector<std::wstring> Infection::zombieNames;

const int ZOMBIE_TEAM = 3;
const int HUMAN_TEAM = 0;
const wchar_t* NEW_ZOMBIE_SOUND = L"sounds/new_zombie.wav";
const wchar_t* INFECTION_SOUND = L"sounds/infection.wav";
const wchar_t* INFECTED_SOUND = L"sounds/infected.wav";

bool firstSpawn;
bool infectedPlayed;
signed int zombiePlayerIndex = -1;

signed int Infection::calculateZombiePlayerIndex() {
	std::vector<int> vecPlayersActiveIndexes;
	network_session* session = NetworkSession::getCurrentNetworkSession();
	int playerIndex = 0;
    do {
		if (players->IsActive(playerIndex))
			vecPlayersActiveIndexes.push_back(playerIndex);

		playerIndex++;
    } while (playerIndex < 16);
	
	if (vecPlayersActiveIndexes.empty())
		return -1;
	else
		return vecPlayersActiveIndexes[rand() % session->membership.total_party_players];
}

void Infection::sendTeamChange()
{
	network_session* session = NetworkSession::getCurrentNetworkSession();
	if (session->local_session_state == _network_session_state_session_host)
	{
		if (session->membership.total_party_players > 0)
		{
			int playerIndex = 0;
			do
			{
				if (players->IsActive(playerIndex))
				{
					if (session->session_host_peer_index != players->getPeerIndexFromPlayerIndex(playerIndex)) {
						CustomPackets::sendTeamChange(session, players->getPeerIndexFromPlayerIndex(playerIndex), zombiePlayerIndex == playerIndex ? ZOMBIE_TEAM : HUMAN_TEAM);
					}
					else
					{
						if (!h2mod->Server)
							h2mod->set_local_team_index(0, zombiePlayerIndex == playerIndex ? ZOMBIE_TEAM : HUMAN_TEAM);
					}
				}
				
				playerIndex++;
			}
			while (playerIndex < 16);
		}
	}
}

Infection::Infection()
{
	this->deinitializer = new InfectionDeinitializer();
	this->initializer = new InfectionInitializer();
	this->preSpawnPlayer = new ZombiePreSpawnHandler();
	this->spawnPlayer = new ZombieSpawnHandler();
	this->playerDeath = new ZombieDeathHandler();
	this->playerKill = new KillZombieHandler();
}

void Infection::triggerSound(const wchar_t* name, int sleep) {
	LOG_TRACE_GAME(L"[h2mod-infection] Triggering sound {}", name);
	h2mod->CustomSoundPlay(name, sleep);
}

void Infection::initClient()
{
	Infection::disableSlayerSounds();
	infectedPlayed = false;
	firstSpawn = true;

	//Change Local Player's Team to Human if Not in Green
	//(In case player wants to start as Alpha Zombie leave him green)
	if (h2mod->get_local_team_index() != ZOMBIE_TEAM) {
		h2mod->set_local_team_index(0, HUMAN_TEAM);
	}
}

void Infection::resetZombiePlayerStatus() {
	Infection::zombieNames.clear();
}

void Infection::setZombiePlayerStatus(int playerIndex)
{
	Infection::zombieNames.push_back(players->getPlayerName(playerIndex));
}

void Infection::initHost() {
	LOG_TRACE_GAME("[h2mod-infection] Host init setting unit speed patch");
	//Applying SpeedCheck fix
	h2mod->set_unit_speed_patch(true);
	LOG_TRACE_GAME("[h2mod-infection] Host init resetting zombie player data status");
	Infection::resetZombiePlayerStatus();
}

void Infection::disableSlayerSounds()
{
	LOG_TRACE_GAME("[h2mod-infection] Disabling slayer sounds");
	//disable slayer sounds for the infection game type
	h2mod->DisableSound(SoundType::Slayer);
	h2mod->DisableSound(SoundType::GainedTheLead);
	h2mod->DisableSound(SoundType::LostTheLead);
	h2mod->DisableSound(SoundType::TeamChange);
}

void Infection::resetWeaponInteractionAndEmblems() {
	LOG_TRACE_GAME("[h2mod-infection] Resetting weapons interactions and emblem visibility");
	h2mod->DisableWeaponPickup(true);
	h2mod->TeamPlayerIndicatorVisibility(true);
}

void Infection::preSpawnServerSetup() {
	int playerIndex = 0;
	do {
		if (players->IsActive(playerIndex)) {
			std::wstring playerName = players->getPlayerName(playerIndex);
			BOOL isZombie = std::find(Infection::zombieNames.begin(), Infection::zombieNames.end(), playerName) != Infection::zombieNames.end();

			LOG_TRACE_GAME(L"[h2mod-infection] Zombie pre spawn index={0}, isZombie={1}, playerName={2}", playerIndex, isZombie, playerName);
			if (isZombie) {
				h2mod->set_unit_biped(Player::Biped::Elite, playerIndex);
				if (NetworkSession::localPeerIsSessionHost()) // prevent kids from switching to humans in the pre-game lobby after joining
					CustomPackets::sendTeamChange(NetworkSession::getCurrentNetworkSession(), players->getPeerIndexFromPlayerIndex(playerIndex), ZOMBIE_TEAM);
			}
			else {
				h2mod->set_unit_biped(Player::Biped::Spartan, playerIndex);
			}
		}
		playerIndex++;
	} while (playerIndex < 16);
}

void Infection::setPlayerAsHuman(int index) {
	h2mod->set_unit_biped(Player::Biped::Spartan, index);
	h2mod->set_unit_speed(1.0f, index);

	GivePlayerWeapon(index, Weapon::shotgun, 1);
	GivePlayerWeapon(index, Weapon::magnum, 0);
}

void Infection::setPlayerAsZombie(int index) {
	h2mod->set_unit_biped(Player::Biped::Elite, index);
	h2mod->set_unit_speed(1.1f, index);

	GivePlayerWeapon(index, Weapon::energy_blade, 1);
}

void Infection::spawnPlayerClientSetup(int playerIndex) {
	wchar_t* playername = h2mod->get_player_name_from_player_index(playerIndex);
	//If player being spawned is LocalUser/Player
	if (wcscmp(playername, h2mod->get_local_player_name()) == 0) {

		if (firstSpawn == true) {
			//start of zombie match
			Infection::triggerSound(INFECTION_SOUND, 1000);
			firstSpawn = false;
		}

		if (h2mod->get_local_team_index() == ZOMBIE_TEAM && infectedPlayed == false) {
			Infection::triggerSound(INFECTED_SOUND, 500);
			infectedPlayed = true;
		}

		if (h2mod->get_local_team_index() == HUMAN_TEAM) {
			h2mod->DisableWeaponPickup(true);
			h2mod->TeamPlayerIndicatorVisibility(false);
		}
		else if (h2mod->get_local_team_index() == ZOMBIE_TEAM) {
			h2mod->set_unit_biped(Player::Biped::Elite, playerIndex);

			h2mod->DisableWeaponPickup(false);
			h2mod->TeamPlayerIndicatorVisibility(true);
		}
	}
}

void Infection::spawnServerPlayerSetup(int index) {
	LOG_TRACE_GAME("[h2mod-infection] Spawn player server index={}", index);
	int unit_datum_index = h2mod->get_unit_datum_from_player_index(index);
	int unit_object = call_get_object(unit_datum_index, 3);

	if (unit_object && *(BYTE*)(unit_object + 0xAA) == 0) {
		//if the unit_object is not 0, the spawned object is "alive"

		LOG_TRACE_GAME("[h2mod-infection] Spawn player server index={0}, unit team index={1}", index, h2mod->get_unit_team_index(unit_datum_index));
		if (h2mod->get_unit_team_index(unit_datum_index) == HUMAN_TEAM)	{
			Infection::setPlayerAsHuman(index);
		}

		if (h2mod->get_unit_team_index(unit_datum_index) == ZOMBIE_TEAM) {
			Infection::setPlayerAsZombie(index);
		}
	}
}

void Infection::infectPlayer(int unitDatumIndex, int playerIndex) {
	int unit_object = call_get_object(unitDatumIndex, 3);
	if (unit_object && h2mod->get_unit_team_index(unitDatumIndex) != ZOMBIE_TEAM
		&& *(BYTE*)(unit_object + 0xAA) == 0) //check if object type is biped
	{
		//if we have a valid object and the object is not on the zombie team
		wchar_t* playername = h2mod->get_player_name_from_player_index(h2mod->get_player_index_from_unit_datum(unitDatumIndex));

		LOG_TRACE_GAME(L"[h2mod-infection] Infected player, localName={0}, nameFromUnitDatumIndex={1}", h2mod->get_local_player_name(), playername);

		//If player being infected is LocalUser/Player
		if (wcscmp(playername, h2mod->get_local_player_name()) == 0) {
			LOG_TRACE_GAME("[h2mod-infection] Setting player as zombie");
			h2mod->set_local_team_index(0, ZOMBIE_TEAM);
			h2mod->set_unit_biped(Player::Biped::Elite, playerIndex);
		}
		else {
			//if not, then this is a new zombie
			Infection::triggerSound(NEW_ZOMBIE_SOUND, 1000);
		}
	}
}

void Infection::infectPlayers(int unitDatumIndex, int playerIndex) {
	int unit_object = call_get_object(unitDatumIndex, 3);
	if (unit_object && *(BYTE*)(unit_object + 0xAA) == 0) {
		Infection::setZombiePlayerStatus(playerIndex);

		if (h2mod->get_unit_team_index(unitDatumIndex) == ZOMBIE_TEAM) {
			//don't drop swords after zombie death
			call_unit_reset_equipment(unitDatumIndex); //Take away zombie's weapons
		}
	}
}

void ZombieDeathHandler::onPeerHost()
{
	int PlayerIndex = h2mod->get_player_index_from_unit_datum(this->getUnitDatumIndex());
	//infect peer host
	Infection::infectPlayer(this->getUnitDatumIndex(), PlayerIndex);

	//infect other players if applicable
	Infection::infectPlayers(this->getUnitDatumIndex(), PlayerIndex);
}

void ZombieDeathHandler::onDedi()
{
	//infect other players if applicable
	Infection::infectPlayers(this->getUnitDatumIndex(), h2mod->get_player_index_from_unit_datum(this->getUnitDatumIndex()));
}

void ZombieDeathHandler::onClient()
{
	LOG_TRACE_GAME("ZombieDeathhandler::OnClient() getUnitDatumIndex: {:x}", this->getUnitDatumIndex());

	//infect client
	Infection::infectPlayer(this->getUnitDatumIndex(), h2mod->get_player_index_from_unit_datum(this->getUnitDatumIndex()));
}

void ZombiePreSpawnHandler::onPeerHost()
{
	this->onClient();
	Infection::preSpawnServerSetup();
}

void ZombiePreSpawnHandler::onDedi()
{
	Infection::preSpawnServerSetup();
}

void ZombiePreSpawnHandler::onClient()
{
	wchar_t* playername = h2mod->get_player_name_from_player_index(this->getPlayerIndex());
	LOG_TRACE_GAME(L"[h2mod-infection] Client pre spawn, playerIndex={0}, playerNameFromIndex={1}, localPlayerName={2}", this->getPlayerIndex(), playername, h2mod->get_local_player_name());
	//If player being spawned is LocalUser/Player
	if (wcscmp(playername, h2mod->get_local_player_name()) == 0)
	{
		LOG_TRACE_GAME("[h2mod-infection] Client pre spawn, found local player, current team = {}", h2mod->get_local_team_index());
		//Change biped if LocalUser is in GreenTeam
		if (h2mod->get_local_team_index() == ZOMBIE_TEAM)
		{
			LOG_TRACE_GAME("[h2mod-infection] Client is infected! switching bipeds: {}\r\n", this->getPlayerIndex());
			h2mod->set_unit_biped(Player::Biped::Elite, this->getPlayerIndex());
		}
	}
}

void ZombieSpawnHandler::onPeerHost()
{
	Infection::spawnPlayerClientSetup(this->getPlayerIndex());
	Infection::spawnServerPlayerSetup(this->getPlayerIndex());
}

void ZombieSpawnHandler::onDedi()
{
	Infection::spawnServerPlayerSetup(this->getPlayerIndex());
}

void ZombieSpawnHandler::onClient()
{
	Infection::spawnPlayerClientSetup(this->getPlayerIndex());
}

void InfectionDeinitializer::onPeerHost()
{
	Infection::resetWeaponInteractionAndEmblems();
	//reset unit speeds on server
	h2mod->set_unit_speed_patch(false);
}

void InfectionDeinitializer::onDedi()
{
	Infection::resetWeaponInteractionAndEmblems();
	//reset unit speeds on server
	h2mod->set_unit_speed_patch(false);
}

void InfectionDeinitializer::onClient()
{
	LOG_TRACE_GAME("[h2mod-infection] Client deinit");
	Infection::resetWeaponInteractionAndEmblems();
}

void InfectionInitializer::onPeerHost()
{
	LOG_TRACE_GAME("[h2mod-infection] Peer host init");
	Infection::initClient();
	Infection::initHost();
	zombiePlayerIndex = Infection::calculateZombiePlayerIndex();
	LOG_TRACE_GAME("[h2mod-infection] Peer host calculated zombie index {}", zombiePlayerIndex);
	if (zombiePlayerIndex == -1) {
		LOG_TRACE_GAME("[h2mod-infection] Failed selecting a zombie!");
	}
	else {
		LOG_TRACE_GAME("[h2mod-infection] Peer host setting player as human");
		//send out the team change packets to peers
		Infection::sendTeamChange();

		//mark this player as the zombie in the internal players structure
		Infection::setZombiePlayerStatus(zombiePlayerIndex);
	}
}

void InfectionInitializer::onDedi()
{
	LOG_TRACE_GAME("[h2mod-infection] Dedicated server init");
	Infection::initHost();
	//figure out who the zombie is
	zombiePlayerIndex = Infection::calculateZombiePlayerIndex();
	LOG_TRACE_GAME("[h2mod-infection] Dedicated host calculated zombie index {}", zombiePlayerIndex);
	if (zombiePlayerIndex == -1) {
		LOG_TRACE_GAME("[h2mod-infection] Failed selecting a zombie!");
	}
	else {
		LOG_TRACE_GAME("[h2mod-infection] Dedicated host setting player as human");
		//send out the team change packets to peers
		Infection::sendTeamChange();

		//mark this player as the zombie in the internal players structure
		Infection::setZombiePlayerStatus(zombiePlayerIndex);
	}
}

void InfectionInitializer::onClient()
{
	LOG_TRACE_GAME("[h2mod-infection] Client init");
	Infection::initClient();
}

void KillZombieHandler::onClient() {
	//implement on kill zombie
}

void KillZombieHandler::onDedi() {
	//implement on kill zombie
}

void KillZombieHandler::onPeerHost() {
	//implement on kill zombie
}

void ZombieHandler::setPlayerIndex(int playerIndex)
{
	this->playerIndex = playerIndex;
}

void ZombieHandler::setUnitDatumIndex(int unitDatumIndex)
{
	this->unitDatumIndex = unitDatumIndex;
}

int ZombieHandler::getPlayerIndex()
{
	return this->playerIndex;
}

int ZombieHandler::getUnitDatumIndex()
{
	return this->unitDatumIndex;
}
