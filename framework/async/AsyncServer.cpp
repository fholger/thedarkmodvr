/*****************************************************************************
The Dark Mod GPL Source Code

This file is part of the The Dark Mod Source Code, originally based
on the Doom 3 GPL Source Code as published in 2011.

The Dark Mod Source Code is free software: you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the License,
or (at your option) any later version. For details, see LICENSE.TXT.

Project: The Dark Mod (http://www.thedarkmod.com/)

******************************************************************************/

#include "precompiled.h"
#pragma hdrstop



#include "AsyncNetwork.h"

#include "../Session_local.h"

const int MIN_RECONNECT_TIME			= 2000;
const int EMPTY_RESEND_TIME				= 500;
const int PING_RESEND_TIME				= 500;
const int NOINPUT_IDLE_TIME				= 30000;

const int HEARTBEAT_MSEC				= 5*60*1000;

// must be kept in sync with authReplyMsg_t
const char* authReplyMsg[] = {
	//	"Waiting for authorization",
	"#str_07204",
	//	"Client unknown to auth",
	"#str_07205",
	//	"Auth custom message", // placeholder - we propagate a message from the master
	"#str_07207",
	//	"Authorize Server - Waiting for client"
	"#str_07208"
};

const char* authReplyStr[] = {
	"AUTH_NONE",
	"AUTH_OK",
	"AUTH_WAIT",
	"AUTH_DENY"
};

/*
==================
idAsyncServer::idAsyncServer
==================
*/
idAsyncServer::idAsyncServer( void ) {
	int i;

	active = false;
	realTime = 0;
	serverTime = 0;
	serverId = 0;
	serverDataChecksum = 0;
	localClientNum = -1;
	gameInitId = 0;
	gameFrame = 0;
	gameTime = 0;
	gameTimeResidual = 0;
	memset( challenges, 0, sizeof( challenges ) );
	memset( userCmds, 0, sizeof( userCmds ) );
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		ClearClient( i );
	}
	serverReloadingEngine = false;
	nextHeartbeatTime = 0;
	nextAsyncStatsTime = 0;
	noRconOutput = true;
	lastAuthTime = 0;

	memset( stats_outrate, 0, sizeof( stats_outrate ) );
	stats_current = 0;
	stats_average_sum = 0;
	stats_max = 0;
	stats_max_index = 0;
}

/*
==================
idAsyncServer::InitPort
==================
*/
bool idAsyncServer::InitPort( void ) {
	int lastPort;

	// if this is the first time we have spawned a server, open the UDP port
	if ( !serverPort.GetPort() ) {
		if ( cvarSystem->GetCVarInteger( "net_port" ) != 0 ) {
			if ( !serverPort.InitForPort( cvarSystem->GetCVarInteger( "net_port" ) ) ) {
				common->Printf( "Unable to open server on port %d (net_port)\n", cvarSystem->GetCVarInteger( "net_port" ) );
				return false;
			}
		} else {
			// scan for multiple ports, in case other servers are running on this IP already
			for ( lastPort = 0; lastPort < NUM_SERVER_PORTS; lastPort++ ) {
				if ( serverPort.InitForPort( PORT_SERVER + lastPort ) ) {
					break;
				}
			}
			if ( lastPort >= NUM_SERVER_PORTS ) {
				common->Printf( "Unable to open server network port.\n" );
				return false;
			}
		}
	}

	return true;
}

/*
==================
idAsyncServer::ClosePort
==================
*/
void idAsyncServer::ClosePort( void ) {
	int i;

	serverPort.Close();
	for ( i = 0; i < MAX_CHALLENGES; i++ ) {
		challenges[ i ].authReplyPrint.Clear();
	}
}

/*
==================
idAsyncServer::Spawn
==================
*/
void idAsyncServer::Spawn( void ) {
	int			i, size;
	byte		msgBuf[MAX_MESSAGE_SIZE];
	netadr_t	from;

	// shutdown any current game
	session->Stop();

	if ( active ) {
		return;
	}

	if ( !InitPort() ) {
		return;
	}

	// trash any currently pending packets
	while( serverPort.GetPacket( from, msgBuf, size, sizeof( msgBuf ) ) ) {
	}

	// reset cheats cvars
	if ( !idAsyncNetwork::allowCheats.GetBool() ) {
		cvarSystem->ResetFlaggedVariables( CVAR_CHEAT );
	}

	memset( challenges, 0, sizeof( challenges ) );
	memset( userCmds, 0, sizeof( userCmds ) );
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		ClearClient( i );
	}

	common->Printf( "Server spawned on port %i.\n", serverPort.GetPort() );

	// calculate a checksum on some of the essential data used
	serverDataChecksum = declManager->GetChecksum();

	// get a pseudo random server id, but don't use the id which is reserved for connectionless packets
	serverId = Sys_Milliseconds() & CONNECTIONLESS_MESSAGE_ID_MASK;

	active = true;

	nextHeartbeatTime = 0;
	nextAsyncStatsTime = 0;

	ExecuteMapChange();
}

/*
==================
idAsyncServer::Kill
==================
*/
void idAsyncServer::Kill( void ) {
	int i, j;

	if ( !active ) {
		return;
	}

	// drop all clients
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		DropClient( i, "#str_07135" );
	}

	// send some empty messages to the zombie clients to make sure they disconnect
	for ( j = 0; j < 4; j++ ) {
		for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
			if ( clients[i].clientState == SCS_ZOMBIE ) {
				if ( clients[i].channel.UnsentFragmentsLeft() ) {
					clients[i].channel.SendNextFragment( serverPort, serverTime );
				} else {
					SendEmptyToClient( i, true );
				}
			}
		}
		Sys_Sleep( 10 );
	}

	active = false;

	// shutdown any current game
	session->Stop();
}

/*
==================
idAsyncServer::ExecuteMapChange
==================
*/
void idAsyncServer::ExecuteMapChange( void ) {
	int			i;
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];
	idStr		mapName;
	findFile_t	ff;
	bool		addonReload = false;
	char		bestGameType[ MAX_STRING_CHARS ];

	assert( active );

	// make sure the map/gametype combo is good
	game->GetBestGameType( cvarSystem->GetCVarString("si_map"), cvarSystem->GetCVarString("si_gametype"), bestGameType );
	cvarSystem->SetCVarString("si_gametype", bestGameType );

	// initialize map settings
	cmdSystem->BufferCommandText( CMD_EXEC_NOW, "rescanSI" );

	sprintf( mapName, "maps/%s", sessLocal.mapSpawnData.serverInfo.GetString( "si_map" ) );
	mapName.SetFileExtension( ".map" );
	ff = fileSystem->FindFile( mapName, !serverReloadingEngine );
	switch( ff ) {
	case FIND_NO:
		common->Printf( "Can't find map %s\n", mapName.c_str() );
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "disconnect\n" );
		return;
	case FIND_ADDON:
		// NOTE: we have no problem with addon dependencies here because if the map is in
		// an addon pack that's already on search list, then all it's deps are assumed to be on search as well
		common->Printf( "map %s is in an addon pak - reloading\n", mapName.c_str() );
		addonReload = true;
		break;
	default:
		break;
	}

	// if we are asked to do a full reload, the strategy is completely different
	if ( !serverReloadingEngine && ( addonReload || idAsyncNetwork::serverReloadEngine.GetInteger() != 0 ) ) {
		if ( idAsyncNetwork::serverReloadEngine.GetInteger() != 0 ) {
			common->Printf( "net_serverReloadEngine enabled - doing a full reload\n" );
		}
		// tell the clients to reconnect
		// in a lot of cases this is going to trigger two reloadEngines for the clients
		// one to restart, the other one to set paks right ( with addon for instance )
		// can fix by reconnecting without reloading and waiting for the server to tell..
		for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
			if ( clients[ i ].clientState >= SCS_CHILL && i != localClientNum ) {
				msg.Init( msgBuf, sizeof( msgBuf ) );
				msg.WriteByte( SERVER_RELIABLE_MESSAGE_RELOAD );
				SendReliableMessage( i, msg );
				clients[ i ].clientState = SCS_ZOMBIE; // so we don't bother sending a disconnect
			}
		}
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "reloadEngine" );
		serverReloadingEngine = true; // don't get caught in endless loop
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "spawnServer\n" );
		// decrease feature
		if ( idAsyncNetwork::serverReloadEngine.GetInteger() > 0 ) {
			idAsyncNetwork::serverReloadEngine.SetInteger( idAsyncNetwork::serverReloadEngine.GetInteger() - 1 );
		}
		return;
	}
	serverReloadingEngine = false;

	serverTime = 0;

	// initialize game id and time
	gameInitId ^= Sys_Milliseconds();	// NOTE: make sure the gameInitId is always a positive number because negative numbers have special meaning
	gameFrame = 0;
	gameTime = 0;
	gameTimeResidual = 0;
	memset( userCmds, 0, sizeof( userCmds ) );

	if ( idAsyncNetwork::serverDedicated.GetInteger() == 0 ) {
		InitLocalClient( 0 );
	} else {
		localClientNum = -1;
	}

	// re-initialize all connected clients for the new map
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		if ( clients[i].clientState >= SCS_CHILL && i != localClientNum ) {

			InitClient( i, clients[i].clientId, clients[i].clientRate );

			SendGameInitToClient( i );
		}
	}

	// load map
	sessLocal.ExecuteMapChange();

	if ( localClientNum >= 0 ) {
		BeginLocalClient();
	} else {
		game->SetLocalClient( -1 );
	}

	// serverTime gets reset, force a heartbeat so timings restart
	MasterHeartbeat( true );
}

/*
==================
idAsyncServer::GetPort
==================
*/
int idAsyncServer::GetPort( void ) const {
	return serverPort.GetPort();
}

/*
===============
idAsyncServer::GetBoundAdr
===============
*/
netadr_t idAsyncServer::GetBoundAdr( void ) const {
	return serverPort.GetAdr();
}

/*
==================
idAsyncServer::GetOutgoingRate
==================
*/
int idAsyncServer::GetOutgoingRate( void ) const {
	int i, rate;

	rate = 0;
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		const serverClient_t &client = clients[i];

		if ( client.clientState >= SCS_CONNECTED ) {
			rate += client.channel.GetOutgoingRate();
		}
	}
	return rate;
}

/*
==================
idAsyncServer::GetIncomingRate
==================
*/
int idAsyncServer::GetIncomingRate( void ) const {
	int i, rate;

	rate = 0;
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		const serverClient_t &client = clients[i];

		if ( client.clientState >= SCS_CONNECTED ) {
			rate += client.channel.GetIncomingRate();
		}
	}
	return rate;
}

/*
==================
idAsyncServer::IsClientInGame
==================
*/
bool idAsyncServer::IsClientInGame( int clientNum ) const {
	return ( clients[clientNum].clientState >= SCS_INGAME );
}

/*
==================
idAsyncServer::GetClientPing
==================
*/
int idAsyncServer::GetClientPing( int clientNum ) const {
	const serverClient_t &client = clients[clientNum];

	if ( client.clientState < SCS_CONNECTED ) {
		return 99999;
	} else {
		return client.clientPing;
	}
}

/*
==================
idAsyncServer::GetClientPrediction
==================
*/
int idAsyncServer::GetClientPrediction( int clientNum ) const {
	const serverClient_t &client = clients[clientNum];

	if ( client.clientState < SCS_CONNECTED ) {
		return 99999;
	} else {
		return client.clientPrediction;
	}
}

/*
==================
idAsyncServer::GetClientTimeSinceLastPacket
==================
*/
int idAsyncServer::GetClientTimeSinceLastPacket( int clientNum ) const {
	const serverClient_t &client = clients[clientNum];

	if ( client.clientState < SCS_CONNECTED ) {
		return 99999;
	} else {
		return serverTime - client.lastPacketTime;
	}
}

/*
==================
idAsyncServer::GetClientTimeSinceLastInput
==================
*/
int idAsyncServer::GetClientTimeSinceLastInput( int clientNum ) const {
	const serverClient_t &client = clients[clientNum];

	if ( client.clientState < SCS_CONNECTED ) {
		return 99999;
	} else {
		return serverTime - client.lastInputTime;
	}
}

/*
==================
idAsyncServer::GetClientOutgoingRate
==================
*/
int idAsyncServer::GetClientOutgoingRate( int clientNum ) const {
	const serverClient_t &client = clients[clientNum];

	if ( client.clientState < SCS_CONNECTED ) {
		return -1;
	} else {
		return client.channel.GetOutgoingRate();
	}
}

/*
==================
idAsyncServer::GetClientIncomingRate
==================
*/
int idAsyncServer::GetClientIncomingRate( int clientNum ) const {
	const serverClient_t &client = clients[clientNum];

	if ( client.clientState < SCS_CONNECTED ) {
		return -1;
	} else {
		return client.channel.GetIncomingRate();
	}
}

/*
==================
idAsyncServer::GetClientOutgoingCompression
==================
*/
float idAsyncServer::GetClientOutgoingCompression( int clientNum ) const {
	const serverClient_t &client = clients[clientNum];

	if ( client.clientState < SCS_CONNECTED ) {
		return 0.0f;
	} else {
		return client.channel.GetOutgoingCompression();
	}
}

/*
==================
idAsyncServer::GetClientIncomingCompression
==================
*/
float idAsyncServer::GetClientIncomingCompression( int clientNum ) const {
	const serverClient_t &client = clients[clientNum];

	if ( client.clientState < SCS_CONNECTED ) {
		return 0.0f;
	} else {
		return client.channel.GetIncomingCompression();
	}
}

/*
==================
idAsyncServer::GetClientIncomingPacketLoss
==================
*/
float idAsyncServer::GetClientIncomingPacketLoss( int clientNum ) const {
	const serverClient_t &client = clients[clientNum];

	if ( client.clientState < SCS_CONNECTED ) {
		return 0.0f;
	} else {
		return client.channel.GetIncomingPacketLoss();
	}
}

/*
==================
idAsyncServer::GetNumClients
==================
*/
int idAsyncServer::GetNumClients( void ) const {
	int ret = 0;
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		if ( clients[ i ].clientState >= SCS_CONNECTED ) {
			ret++;
		}
	}
	return ret;
}

/*
==================
idAsyncServer::GetNumIdleClients
==================
*/
int idAsyncServer::GetNumIdleClients( void ) const {
	int ret = 0;
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		if ( clients[ i ].clientState >= SCS_CONNECTED ) {
			if ( serverTime - clients[ i ].lastInputTime > NOINPUT_IDLE_TIME ) {
				ret++;
			}
		}
	}
	return ret;
}

/*
==================
idAsyncServer::DuplicateUsercmds
==================
*/
void idAsyncServer::DuplicateUsercmds( int frame, int time ) {
	int i, previousIndex, currentIndex;

	previousIndex = ( frame - 1 ) & ( MAX_USERCMD_BACKUP - 1 );
	currentIndex = frame & ( MAX_USERCMD_BACKUP - 1 );

	// duplicate previous user commands if no new commands are available for a client
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		if ( clients[i].clientState == SCS_FREE ) {
			continue;
		}

		if ( idAsyncNetwork::DuplicateUsercmd( userCmds[previousIndex][i], userCmds[currentIndex][i], frame, time ) ) {
			clients[i].numDuplicatedUsercmds++;
		}
	}
}

/*
==================
idAsyncServer::ClearClient
==================
*/
void idAsyncServer::ClearClient( int clientNum ) {
	serverClient_t &client = clients[clientNum];
	client.clientId = 0;
	client.clientState = SCS_FREE;
	client.clientPrediction = 0;
	client.clientAheadTime = 0;
	client.clientRate = 0;
	client.clientPing = 0;
	client.gameInitSequence = 0;
	client.gameFrame = 0;
	client.gameTime = 0;
	client.channel.Shutdown();
	client.lastConnectTime = 0;
	client.lastEmptyTime = 0;
	client.lastPingTime = 0;
	client.lastSnapshotTime = 0;
	client.lastPacketTime = 0;
	client.lastInputTime = 0;
	client.snapshotSequence = 0;
	client.acknowledgeSnapshotSequence = 0;
	client.numDuplicatedUsercmds = 0;
}

/*
==================
idAsyncServer::InitClient
==================
*/
void idAsyncServer::InitClient( int clientNum, int clientId, int clientRate ) {
	int i;

	// clear the user info
	sessLocal.mapSpawnData.userInfo[ clientNum ].Clear();	// always start with a clean base

	// clear the server client
	serverClient_t &client = clients[clientNum];
	client.clientId = clientId;
	client.clientState = SCS_CONNECTED;
	client.clientPrediction = 0;
	client.clientAheadTime = 0;
	client.gameInitSequence = -1;
	client.gameFrame = 0;
	client.gameTime = 0;
	client.channel.ResetRate();
	client.clientRate = clientRate ? clientRate : idAsyncNetwork::serverMaxClientRate.GetInteger();
	client.channel.SetMaxOutgoingRate( Min( idAsyncNetwork::serverMaxClientRate.GetInteger(), client.clientRate ) );
	client.clientPing = 0;
	client.lastConnectTime = serverTime;
	client.lastEmptyTime = serverTime;
	client.lastPingTime = serverTime;
	client.lastSnapshotTime = serverTime;
	client.lastPacketTime = serverTime;
	client.lastInputTime = serverTime;
	client.acknowledgeSnapshotSequence = 0;
	client.numDuplicatedUsercmds = 0;

	// clear the user commands
	for ( i = 0; i < MAX_USERCMD_BACKUP; i++ ) {
		memset( &userCmds[i][clientNum], 0, sizeof( userCmds[i][clientNum] ) );
	}

	// let the game know a player connected
	game->ServerClientConnect( clientNum, client.guid );
}

/*
==================
idAsyncServer::InitLocalClient
==================
*/
void idAsyncServer::InitLocalClient( int clientNum ) {
	netadr_t badAddress;

	localClientNum = clientNum;
	InitClient( clientNum, 0, 0 );
	memset( &badAddress, 0, sizeof( badAddress ) );
	badAddress.type = NA_BAD;
	clients[clientNum].channel.Init( badAddress, serverId );
	clients[clientNum].clientState = SCS_INGAME;
	sessLocal.mapSpawnData.userInfo[clientNum] = *cvarSystem->MoveCVarsToDict( CVAR_USERINFO );
}

/*
==================
idAsyncServer::BeginLocalClient
==================
*/
void idAsyncServer::BeginLocalClient( void ) {
	game->SetLocalClient( localClientNum );
	game->SetUserInfo( localClientNum, sessLocal.mapSpawnData.userInfo[localClientNum], false, false );
	game->ServerClientBegin( localClientNum );
}

/*
==================
idAsyncServer::LocalClientInput
==================
*/
void idAsyncServer::LocalClientInput( void ) {
	int index;

	if ( localClientNum < 0 ) {
		return;
	}

	index = gameFrame & ( MAX_USERCMD_BACKUP - 1 );
	userCmds[index][localClientNum] = usercmdGen->GetDirectUsercmd();
	userCmds[index][localClientNum].gameFrame = gameFrame;
	userCmds[index][localClientNum].gameTime = gameTime;
	if ( idAsyncNetwork::UsercmdInputChanged( userCmds[( gameFrame - 1 ) & ( MAX_USERCMD_BACKUP - 1 )][localClientNum], userCmds[index][localClientNum] ) ) {
		clients[localClientNum].lastInputTime = serverTime;
	}
	clients[localClientNum].gameFrame = gameFrame;
	clients[localClientNum].gameTime = gameTime;
	clients[localClientNum].lastPacketTime = serverTime;
}

/*
==================
idAsyncServer::DropClient
==================
*/
void idAsyncServer::DropClient( int clientNum, const char *reason ) {
	int			i;
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	serverClient_t &client = clients[clientNum];

	if ( client.clientState <= SCS_ZOMBIE ) {
		return;
	}

	if ( client.clientState >= SCS_CHILL && clientNum != localClientNum ) {
		msg.Init( msgBuf, sizeof( msgBuf ) );
		msg.WriteByte( SERVER_RELIABLE_MESSAGE_DISCONNECT );
		msg.WriteInt( clientNum );
		msg.WriteString( reason );
		for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
			// clientNum so SCS_CHILL client gets it's own disconnect msg
			if ( i == clientNum || clients[i].clientState >= SCS_CONNECTED ) {
				SendReliableMessage( i, msg );
			}
		}
	}

	reason = common->Translate( reason );
	common->Printf( "client %d %s\n", clientNum, reason );
	cmdSystem->BufferCommandText( CMD_EXEC_NOW, va( "addChatLine \"%s^0 %s\"", sessLocal.mapSpawnData.userInfo[ clientNum ].GetString( "ui_name" ), reason ) );

	// remove the player from the game
	game->ServerClientDisconnect( clientNum );

	client.clientState = SCS_ZOMBIE;
}

/*
==================
idAsyncServer::SendReliableMessage
==================
*/
void idAsyncServer::SendReliableMessage( int clientNum, const idBitMsg &msg ) {
	if ( clientNum == localClientNum ) {
		return;
	}
	if ( !clients[ clientNum ].channel.SendReliableMessage( msg ) ) {
		clients[ clientNum ].channel.ClearReliableMessages();
		DropClient( clientNum, "#str_07136" );
	}
}

/*
==================
idAsyncServer::CheckClientTimeouts
==================
*/
void idAsyncServer::CheckClientTimeouts( void ) {
	int i, zombieTimeout, clientTimeout;

	zombieTimeout = serverTime - idAsyncNetwork::serverZombieTimeout.GetInteger() * 1000;
	clientTimeout = serverTime - idAsyncNetwork::serverClientTimeout.GetInteger() * 1000;

	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		serverClient_t &client = clients[i];

		if ( i == localClientNum ) {
			continue;
		}

		if ( client.lastPacketTime > serverTime ) {
			client.lastPacketTime = serverTime;
			continue;
		}

		if ( client.clientState == SCS_ZOMBIE && client.lastPacketTime < zombieTimeout ) {
			client.channel.Shutdown();
			client.clientState = SCS_FREE;
			continue;
		}

		if ( client.clientState >= SCS_CHILL && client.lastPacketTime < clientTimeout ) {
			DropClient( i, "#str_07137" );
			continue;
		}
	}
}

/*
==================
idAsyncServer::SendPrintBroadcast
==================
*/
void idAsyncServer::SendPrintBroadcast( const char *string ) {
	int			i;
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteByte( SERVER_RELIABLE_MESSAGE_PRINT );
	msg.WriteString( string );

	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		if ( clients[i].clientState >= SCS_CONNECTED ) {
			SendReliableMessage( i, msg );
		}
	}
}

/*
==================
idAsyncServer::SendPrintToClient
==================
*/
void idAsyncServer::SendPrintToClient( int clientNum, const char *string ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	serverClient_t &client = clients[clientNum];

	if ( client.clientState < SCS_CONNECTED ) {
		return;
	}

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteByte( SERVER_RELIABLE_MESSAGE_PRINT );
	msg.WriteString( string );

	SendReliableMessage( clientNum, msg );
}

/*
==================
idAsyncServer::SendUserInfoBroadcast
==================
*/
void idAsyncServer::SendUserInfoBroadcast( int userInfoNum, const idDict &info, bool sendToAll ) {
	idBitMsg		msg;
	byte			msgBuf[MAX_MESSAGE_SIZE];
	const idDict	*gameInfo;
	bool			gameModifiedInfo;

	gameInfo = game->SetUserInfo( userInfoNum, info, false, true );
	if ( gameInfo ) {
		gameModifiedInfo = true;
	} else {
		gameModifiedInfo = false;
		gameInfo = &info;
	}

	if ( userInfoNum == localClientNum ) {
		common->DPrintf( "local user info modified by server\n" );
		cvarSystem->SetCVarsFromDict( *gameInfo );
		cvarSystem->ClearModifiedFlags( CVAR_USERINFO ); // don't emit back
	}

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteByte( SERVER_RELIABLE_MESSAGE_CLIENTINFO );
	msg.WriteByte( userInfoNum );
	if ( gameModifiedInfo || sendToAll ) {
		msg.WriteBits( 0, 1 );
	} else {
		msg.WriteBits( 1, 1 );
	}

#if ID_CLIENTINFO_TAGS
	msg.WriteInt( sessLocal.mapSpawnData.userInfo[userInfoNum].Checksum() );
	common->DPrintf( "broadcast for client %d: 0x%x\n", userInfoNum, sessLocal.mapSpawnData.userInfo[userInfoNum].Checksum() );
	sessLocal.mapSpawnData.userInfo[userInfoNum].Print();
#endif

	if ( gameModifiedInfo || sendToAll ) {
		msg.WriteDeltaDict( *gameInfo, NULL );
	} else {
		msg.WriteDeltaDict( *gameInfo, &sessLocal.mapSpawnData.userInfo[userInfoNum] );
	}

	for ( int i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		if ( clients[i].clientState >= SCS_CONNECTED && ( sendToAll || i != userInfoNum || gameModifiedInfo ) ) {
			SendReliableMessage( i, msg );
		}
	}

	sessLocal.mapSpawnData.userInfo[userInfoNum] = *gameInfo;
}

/*
==================
idAsyncServer::UpdateUI
if the game modifies userInfo, it will call this through command system
we then need to get the info from the game, and broadcast to clients
( using DeltaDict and our current mapSpawnData as a base )
==================
*/
void idAsyncServer::UpdateUI( int clientNum ) {
	const idDict	*info = game->GetUserInfo( clientNum );

	if ( !info ) {
		common->Warning( "idAsyncServer::UpdateUI: no info from game" );
		return;
	}

	SendUserInfoBroadcast( clientNum, *info, true );
}

/*
==================
idAsyncServer::SendUserInfoToClient
==================
*/
void idAsyncServer::SendUserInfoToClient( int clientNum, int userInfoNum, const idDict &info ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	if ( clients[clientNum].clientState < SCS_CONNECTED ) {
		return;
	}

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteByte( SERVER_RELIABLE_MESSAGE_CLIENTINFO );
	msg.WriteByte( userInfoNum );
	msg.WriteBits( 0, 1 );

#if ID_CLIENTINFO_TAGS
	msg.WriteInt( 0 );
	common->DPrintf( "user info %d to client %d: NULL base\n", userInfoNum, clientNum );
#endif

	msg.WriteDeltaDict( info, NULL );

	SendReliableMessage( clientNum, msg );
}

/*
==================
idAsyncServer::SendSyncedCvarsBroadcast
==================
*/
void idAsyncServer::SendSyncedCvarsBroadcast( const idDict &cvars ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];
	int			i;

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteByte( SERVER_RELIABLE_MESSAGE_SYNCEDCVARS );
	msg.WriteDeltaDict( cvars, &sessLocal.mapSpawnData.syncedCVars );

	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		if ( clients[i].clientState >= SCS_CONNECTED ) {
			SendReliableMessage( i, msg );
		}
	}

	sessLocal.mapSpawnData.syncedCVars = cvars;
}

/*
==================
idAsyncServer::SendSyncedCvarsToClient
==================
*/
void idAsyncServer::SendSyncedCvarsToClient( int clientNum, const idDict &cvars ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	if ( clients[clientNum].clientState < SCS_CONNECTED ) {
		return;
	}

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteByte( SERVER_RELIABLE_MESSAGE_SYNCEDCVARS );
	msg.WriteDeltaDict( cvars, NULL );

	SendReliableMessage( clientNum, msg );
}

/*
==================
idAsyncServer::SendApplySnapshotToClient
==================
*/
void idAsyncServer::SendApplySnapshotToClient( int clientNum, int sequence ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteByte( SERVER_RELIABLE_MESSAGE_APPLYSNAPSHOT );
	msg.WriteInt( sequence );

	SendReliableMessage( clientNum, msg );
}

/*
==================
idAsyncServer::SendEmptyToClient
==================
*/
bool idAsyncServer::SendEmptyToClient( int clientNum, bool force ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	serverClient_t &client = clients[clientNum];

	if ( client.lastEmptyTime > realTime ) {
		client.lastEmptyTime = realTime;
	}

	if ( !force && ( realTime - client.lastEmptyTime < EMPTY_RESEND_TIME ) ) {
		return false;
	}

	if ( idAsyncNetwork::verbose.GetInteger() ) {
		common->Printf( "sending empty to client %d: gameInitId = %d, gameFrame = %d, gameTime = %d\n", clientNum, gameInitId, gameFrame, gameTime );
	}

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteInt( gameInitId );
	msg.WriteByte( SERVER_UNRELIABLE_MESSAGE_EMPTY );

	client.channel.SendMessage( serverPort, serverTime, msg );

	client.lastEmptyTime = realTime;

	return true;
}

/*
==================
idAsyncServer::SendPingToClient
==================
*/
bool idAsyncServer::SendPingToClient( int clientNum ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	serverClient_t &client = clients[clientNum];

	if ( client.lastPingTime > realTime ) {
		client.lastPingTime = realTime;
	}

	if ( realTime - client.lastPingTime < PING_RESEND_TIME ) {
		return false;
	}

	if ( idAsyncNetwork::verbose.GetInteger() == 2 ) {
		common->Printf( "pinging client %d: gameInitId = %d, gameFrame = %d, gameTime = %d\n", clientNum, gameInitId, gameFrame, gameTime );
	}

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteInt( gameInitId );
	msg.WriteByte( SERVER_UNRELIABLE_MESSAGE_PING );
	msg.WriteInt( realTime );

	client.channel.SendMessage( serverPort, serverTime, msg );

	client.lastPingTime = realTime;

	return true;
}

/*
==================
idAsyncServer::SendGameInitToClient
==================
*/
void idAsyncServer::SendGameInitToClient( int clientNum ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	if ( idAsyncNetwork::verbose.GetInteger() ) {
		common->Printf( "sending gameinit to client %d: gameInitId = %d, gameFrame = %d, gameTime = %d\n", clientNum, gameInitId, gameFrame, gameTime );
	}

	serverClient_t &client = clients[clientNum];

	// clear the unsent fragments. might flood winsock but that's ok
	while( client.channel.UnsentFragmentsLeft() ) {
		client.channel.SendNextFragment( serverPort, serverTime );
	}			

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteInt( gameInitId );
	msg.WriteByte( SERVER_UNRELIABLE_MESSAGE_GAMEINIT );
	msg.WriteInt( gameFrame );
	msg.WriteInt( gameTime );
	msg.WriteDeltaDict( sessLocal.mapSpawnData.serverInfo, NULL );
	client.gameInitSequence = client.channel.SendMessage( serverPort, serverTime, msg );
}

/*
==================
idAsyncServer::SendSnapshotToClient
==================
*/
bool idAsyncServer::SendSnapshotToClient( int clientNum ) {
	int			i, j, index, numUsercmds;
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];
	usercmd_t *	last;
	byte		clientInPVS[MAX_ASYNC_CLIENTS >> 3];

	serverClient_t &client = clients[clientNum];

	if ( serverTime - client.lastSnapshotTime < idAsyncNetwork::serverSnapshotDelay.GetInteger() ) {
		return false;
	}

	if ( idAsyncNetwork::verbose.GetInteger() == 2 ) {
		common->Printf( "sending snapshot to client %d: gameInitId = %d, gameFrame = %d, gameTime = %d\n", clientNum, gameInitId, gameFrame, gameTime );
	}

	// how far is the client ahead of the server minus the packet delay
	client.clientAheadTime = client.gameTime - ( gameTime + gameTimeResidual );

	// write the snapshot
	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteInt( gameInitId );
	msg.WriteByte( SERVER_UNRELIABLE_MESSAGE_SNAPSHOT );
	msg.WriteInt( client.snapshotSequence );
	msg.WriteInt( gameFrame );
	msg.WriteInt( gameTime );
	msg.WriteByte( idMath::ClampChar( client.numDuplicatedUsercmds ) );
	msg.WriteShort( idMath::ClampShort( client.clientAheadTime ) );

	// write the game snapshot
	game->ServerWriteSnapshot( clientNum, client.snapshotSequence, msg, clientInPVS, MAX_ASYNC_CLIENTS );

	// write the latest user commands from the other clients in the PVS to the snapshot
	for ( last = NULL, i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		serverClient_t &client = clients[i];

		if ( client.clientState == SCS_FREE || i == clientNum ) {
			continue;
		}

		// if the client is not in the PVS
		if ( !( clientInPVS[i >> 3] & ( 1 << ( i & 7 ) ) ) ) {
			continue;
		}

		int maxRelay = idMath::ClampInt( 1, MAX_USERCMD_RELAY, idAsyncNetwork::serverMaxUsercmdRelay.GetInteger() );

		// Max( 1, to always send at least one cmd, which we know we have because we call DuplicateUsercmds in RunFrame
		numUsercmds = Max( 1, Min( client.gameFrame, gameFrame + maxRelay ) - gameFrame );
		msg.WriteByte( i );
		msg.WriteByte( numUsercmds );
		for ( j = 0; j < numUsercmds; j++ ) {
			index = ( gameFrame + j ) & ( MAX_USERCMD_BACKUP - 1 );
			idAsyncNetwork::WriteUserCmdDelta( msg, userCmds[index][i], last );
			last = &userCmds[index][i];
		}
	}
	msg.WriteByte( MAX_ASYNC_CLIENTS );

	client.channel.SendMessage( serverPort, serverTime, msg );

	client.lastSnapshotTime = serverTime;
	client.snapshotSequence++;
	client.numDuplicatedUsercmds = 0;

	return true;
}

/*
==================
idAsyncServer::ProcessUnreliableClientMessage
==================
*/
void idAsyncServer::ProcessUnreliableClientMessage( int clientNum, const idBitMsg &msg ) {
	int i, id, acknowledgeSequence, clientGameInitId, clientGameFrame, numUsercmds, index;
	usercmd_t *last;

	serverClient_t &client = clients[clientNum];

	if ( client.clientState == SCS_ZOMBIE ) {
		return;
	}

	acknowledgeSequence = msg.ReadInt();
	clientGameInitId = msg.ReadInt();

	// while loading a map the client may send empty messages to keep the connection alive
	if ( clientGameInitId == GAME_INIT_ID_MAP_LOAD ) {
		if ( idAsyncNetwork::verbose.GetInteger() ) {			
			common->Printf( "ignore unreliable msg from client %d, gameInitId == ID_MAP_LOAD\n", clientNum );
		}
		return;
	}

	// check if the client is in the right game
	if ( clientGameInitId != gameInitId ) {
		if ( acknowledgeSequence > client.gameInitSequence ) {
			// the client is connected but not in the right game
			client.clientState = SCS_CONNECTED;

			// send game init to client
			SendGameInitToClient( clientNum );

		} else if ( idAsyncNetwork::verbose.GetInteger() ) {
			common->Printf( "ignore unreliable msg from client %d, wrong gameInit, old sequence\n", clientNum );
		}
		return;
	}

	client.acknowledgeSnapshotSequence = msg.ReadInt();

	if ( client.clientState == SCS_CONNECTED ) {

		// the client is in the right game
		client.clientState = SCS_INGAME;

		// send the user info of other clients
		for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
			if ( clients[i].clientState >= SCS_CONNECTED && i != clientNum ) {
				SendUserInfoToClient( clientNum, i, sessLocal.mapSpawnData.userInfo[i] );
			}
		}

		// send synchronized cvars to client
		SendSyncedCvarsToClient( clientNum, sessLocal.mapSpawnData.syncedCVars );

		SendEnterGameToClient( clientNum );

		// get the client running in the game
		game->ServerClientBegin( clientNum );

		// write any reliable messages to initialize the client game state
		game->ServerWriteInitialReliableMessages( clientNum );
	} else if ( client.clientState == SCS_INGAME ) {

		// apply the last snapshot the client received
		if ( game->ServerApplySnapshot( clientNum, client.acknowledgeSnapshotSequence ) ) {
			SendApplySnapshotToClient( clientNum, client.acknowledgeSnapshotSequence );
		}
	}

	// process the unreliable message
	id = msg.ReadByte();
	switch( id ) {
		case CLIENT_UNRELIABLE_MESSAGE_EMPTY: {
			if ( idAsyncNetwork::verbose.GetInteger() ) {
				common->Printf( "received empty message for client %d\n", clientNum );
			}
			break;
		}
		case CLIENT_UNRELIABLE_MESSAGE_PINGRESPONSE: {
			client.clientPing = realTime - msg.ReadInt();
			break;
		}
		case CLIENT_UNRELIABLE_MESSAGE_USERCMD: {

			client.clientPrediction = msg.ReadShort();

			// read user commands
			clientGameFrame = msg.ReadInt();
			numUsercmds = msg.ReadByte();
			for ( last = NULL, i = clientGameFrame - numUsercmds + 1; i <= clientGameFrame; i++ ) {
				index = i & ( MAX_USERCMD_BACKUP - 1 );
				idAsyncNetwork::ReadUserCmdDelta( msg, userCmds[index][clientNum], last );
				userCmds[index][clientNum].gameFrame = i;
				userCmds[index][clientNum].duplicateCount = 0;
				if ( idAsyncNetwork::UsercmdInputChanged( userCmds[( i - 1 ) & ( MAX_USERCMD_BACKUP - 1 )][clientNum], userCmds[index][clientNum] ) ) {
					client.lastInputTime = serverTime;
				}
				last = &userCmds[index][clientNum];
			}

			if ( last ) {
				client.gameFrame = last->gameFrame;
				client.gameTime = last->gameTime;
			}

			if ( idAsyncNetwork::verbose.GetInteger() == 2 ) {
				common->Printf( "received user command for client %d, gameInitId = %d, gameFrame, %d gameTime %d\n", clientNum, clientGameInitId, client.gameFrame, client.gameTime );
			}
			break;
		}
		default: {
			common->Printf( "unknown unreliable message %d from client %d\n", id, clientNum );
			break;
		}
	}
}

/*
==================
idAsyncServer::ProcessReliableClientMessages
==================
*/
void idAsyncServer::ProcessReliableClientMessages( int clientNum ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];
	byte		id;

	serverClient_t &client = clients[clientNum];

	msg.Init( msgBuf, sizeof( msgBuf ) );

	while ( client.channel.GetReliableMessage( msg ) ) {
		id = msg.ReadByte();
		switch( id ) {
			case CLIENT_RELIABLE_MESSAGE_CLIENTINFO: {
				idDict info;
				msg.ReadDeltaDict( info, &sessLocal.mapSpawnData.userInfo[clientNum] );
				SendUserInfoBroadcast( clientNum, info );
				break;
			}
			case CLIENT_RELIABLE_MESSAGE_PRINT: {
				char string[MAX_STRING_CHARS];
				msg.ReadString( string, sizeof( string ) );
				common->Printf( "%s\n", string );
				break;
			}
			case CLIENT_RELIABLE_MESSAGE_DISCONNECT: {
				DropClient( clientNum, "#str_07138" );
				break;
			}
			default: {
				// pass reliable message on to game code
				game->ServerProcessReliableMessage( clientNum, msg );
				break;
			}
		}
	}
}

/*
==================
idAsyncServer::ProcessAuthMessage
==================
*/
void idAsyncServer::ProcessAuthMessage( const idBitMsg &msg ) {
	netadr_t		client_from;
	char			client_guid[ 12 ], string[ MAX_STRING_CHARS ];
	int				i, clientId;
	authReply_t		reply;
	authReplyMsg_t	replyMsg = AUTH_REPLY_WAITING;
	idStr			replyPrintMsg;
	
	reply = (authReply_t)msg.ReadByte();
	if ( reply <= 0 || reply >= AUTH_MAXSTATES ) {
		common->DPrintf( "auth: invalid reply %d\n", reply );
		return;
	}
	clientId = msg.ReadShort( );
	msg.ReadNetadr( &client_from );
	msg.ReadString( client_guid, sizeof( client_guid ) );
	if ( reply != AUTH_OK ) {		
		replyMsg = (authReplyMsg_t)msg.ReadByte();
		if ( replyMsg <= 0 || replyMsg >= AUTH_REPLY_MAXSTATES ) {
			common->DPrintf( "auth: invalid reply msg %d\n", replyMsg );
			return;
		}
		if ( replyMsg == AUTH_REPLY_PRINT ) {
			msg.ReadString( string, MAX_STRING_CHARS );
			replyPrintMsg = string;
		}
	}

	lastAuthTime = serverTime;

	// no message parsing below
	
	for ( i = 0; i < MAX_CHALLENGES; i++ ) {
		if ( !challenges[i].connected && challenges[ i ].clientId == clientId ) {
			// return if something is wrong
			// break if we have found a valid auth
			if ( !strlen( challenges[ i ].guid ) ) {
				common->DPrintf( "auth: client %s has no guid yet\n", Sys_NetAdrToString( challenges[ i ].address ) );
				return;
			}
			if ( idStr::Cmp( challenges[ i ].guid, client_guid ) ) {
				common->DPrintf( "auth: client %s %s not matched, auth server says guid %s\n", Sys_NetAdrToString( challenges[ i ].address ), challenges[i].guid, client_guid );
				return;
			}
			if ( !Sys_CompareNetAdrBase( client_from, challenges[i].address ) ) {
				// let auth work when server and master don't see the same IP
				common->DPrintf( "auth: matched guid '%s' for != IPs %s and %s\n", client_guid, Sys_NetAdrToString( client_from ), Sys_NetAdrToString( challenges[i].address ) );
			}
			break;
		}
	}
	if ( i >= MAX_CHALLENGES ) {
		common->DPrintf( "auth: failed client lookup %s %s\n", Sys_NetAdrToString( client_from ), client_guid );
		return;
	}

	if ( challenges[ i ].authState != CDK_WAIT ) {
		common->DWarning( "auth: challenge 0x%x %s authState %d != CDK_WAIT", challenges[ i ].challenge, Sys_NetAdrToString( challenges[ i ].address ), challenges[ i ].authState );
		return;
	}
	
	idStr::snPrintf( challenges[ i ].guid, 12, client_guid );
	if ( reply == AUTH_OK ) {
		challenges[ i ].authState = CDK_OK;
		common->Printf( "client %s %s is authed\n", Sys_NetAdrToString( client_from ), client_guid );
	} else {
		const char *msg;
		if ( replyMsg != AUTH_REPLY_PRINT ) {
			msg = authReplyMsg[ replyMsg ];
		} else {
			msg = replyPrintMsg.c_str();
		}
		// maybe localize it
		const char *l_msg = common->Translate( msg );
		common->DPrintf( "auth: client %s %s - %s %s\n", Sys_NetAdrToString( client_from ), client_guid, authReplyStr[ reply ], l_msg );
		challenges[ i ].authReply = reply;
		challenges[ i ].authReplyMsg = replyMsg;
		challenges[ i ].authReplyPrint = replyPrintMsg;
	}
}

/*
==================
idAsyncServer::ProcessChallengeMessage
==================
*/
void idAsyncServer::ProcessChallengeMessage( const netadr_t from, const idBitMsg &msg ) {
	int			i, clientId, oldest, oldestTime;
	idBitMsg	outMsg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	clientId = msg.ReadInt();

	oldest = 0;
	oldestTime = 0x7fffffff;

	// see if we already have a challenge for this ip
	for ( i = 0; i < MAX_CHALLENGES; i++ ) {
		if ( !challenges[i].connected && Sys_CompareNetAdrBase( from, challenges[i].address ) && clientId == challenges[i].clientId ) {
			break;
		}
		if ( challenges[i].time < oldestTime ) {
			oldestTime = challenges[i].time;
			oldest = i;
		}
	}

	if ( i >= MAX_CHALLENGES ) {
		// this is the first time this client has asked for a challenge
		i = oldest;
		challenges[i].address = from;
		challenges[i].clientId = clientId;
		challenges[i].challenge = ( (rand() << 16) ^ rand() ) ^ serverTime;
		challenges[i].time = serverTime;
		challenges[i].connected = false;
		challenges[i].authState = CDK_WAIT;
		challenges[i].authReply = AUTH_NONE;
		challenges[i].authReplyMsg = AUTH_REPLY_WAITING;
		challenges[i].authReplyPrint = "";
		challenges[i].guid[0] = '\0';
	}
	challenges[i].pingTime = serverTime;

	common->Printf( "sending challenge 0x%x to %s\n", challenges[i].challenge, Sys_NetAdrToString( from ) );

	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
	outMsg.WriteString( "challengeResponse" );
	outMsg.WriteInt( challenges[i].challenge );
	outMsg.WriteShort( serverId );
	outMsg.WriteString( cvarSystem->GetCVarString( "fs_mod" ) );
	outMsg.WriteString( cvarSystem->GetCVarString( "fs_currentfm" ) );

	serverPort.SendPacket( from, outMsg.GetData(), outMsg.GetSize() );

	if ( Sys_IsLANAddress( from ) ) {
		// no CD Key check for LAN clients
		challenges[i].authState = CDK_OK;
	} else {
		if ( idAsyncNetwork::LANServer.GetBool() ) {
			common->Printf( "net_LANServer is enabled. Client %s is not a LAN address, will be rejected\n", Sys_NetAdrToString( from ) );
			challenges[ i ].authState = CDK_ONLYLAN;
		} else {
			// emit a cd key confirmation request
			outMsg.BeginWriting();
			outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
			outMsg.WriteString( "srvAuth" );
			outMsg.WriteInt( ASYNC_PROTOCOL_VERSION );
			outMsg.WriteNetadr( from );
			outMsg.WriteInt( -1 ); // this identifies "challenge" auth vs "connect" auth
			// protocol 1.37 addition
			// might expect : outMsg.WriteByte( fileSystem->RunningD3XP() );
			serverPort.SendPacket( idAsyncNetwork::GetMasterAddress(), outMsg.GetData(), outMsg.GetSize() );
		}
	}
}

/*
==================
idAsyncServer::ValidateChallenge
==================
*/
int idAsyncServer::ValidateChallenge( const netadr_t from, int challenge, int clientId ) {
	int i;
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		const serverClient_t &client = clients[i];

		if ( client.clientState == SCS_FREE ) {
			continue;
		}
		if ( Sys_CompareNetAdrBase( from, client.channel.GetRemoteAddress() ) &&
					( clientId == client.clientId || from.port == client.channel.GetRemoteAddress().port ) ) {
			if ( serverTime - client.lastConnectTime < MIN_RECONNECT_TIME ) {
				common->Printf( "%s: reconnect rejected : too soon\n", Sys_NetAdrToString( from ) );
				return -1;
			}
			break;
		}
	}

	for ( i = 0; i < MAX_CHALLENGES; i++ ) {
		if ( Sys_CompareNetAdrBase( from, challenges[i].address ) && from.port == challenges[i].address.port ) {
			if ( challenge == challenges[i].challenge ) {
				break;
			}
		}
	}
	if ( i == MAX_CHALLENGES ) {
		PrintOOB( from, SERVER_PRINT_BADCHALLENGE, "#str_04840" );
		return -1;
	}
	return i;
}

/*
==================
idAsyncServer::ProcessConnectMessage
==================
*/
void idAsyncServer::ProcessConnectMessage( const netadr_t from, const idBitMsg &msg ) {
	int			clientNum, protocol, clientDataChecksum, challenge, clientId, ping, clientRate;
	idBitMsg	outMsg;
	byte		msgBuf[ MAX_MESSAGE_SIZE ];
	char		guid[ 12 ];
	char		password[ 17 ];
	int			i, ichallenge, islot, OS, numClients;

	protocol = msg.ReadInt();
	OS = msg.ReadShort();

	// check the protocol version
	if ( protocol != ASYNC_PROTOCOL_VERSION ) {
		// that's a msg back to a client, we don't know about it's localization, so send english
		PrintOOB( from, SERVER_PRINT_BADPROTOCOL, va( "server uses protocol %d.%d\n", ASYNC_PROTOCOL_MAJOR, ASYNC_PROTOCOL_MINOR ) );
		return;
	}

	clientDataChecksum = msg.ReadInt();
	challenge = msg.ReadInt();
	clientId = msg.ReadShort();
	clientRate = msg.ReadInt();

	// check the client data
	if ( clientDataChecksum != serverDataChecksum ) {
		PrintOOB( from, SERVER_PRINT_MISC, "#str_04842" );
		return;
	}

	if ( ( ichallenge = ValidateChallenge( from, challenge, clientId ) ) == -1 ) {
		return;
	}
	challenges[ ichallenge ].OS = OS;

	msg.ReadString( guid, sizeof( guid ) );

	switch ( challenges[ ichallenge ].authState ) {
		case CDK_ONLYLAN:
			common->DPrintf( "%s: not a lan client\n", Sys_NetAdrToString( from ) );
			PrintOOB( from, SERVER_PRINT_MISC, "#str_04843" );
			return;
		case CDK_WAIT:
			if ( challenges[ ichallenge ].authReply == AUTH_NONE && Min( serverTime - lastAuthTime, serverTime - challenges[ ichallenge ].time ) > AUTHORIZE_TIMEOUT ) {
				common->DPrintf( "%s: Authorize server timed out\n", Sys_NetAdrToString( from ) );
				break; // will continue with the connecting process
			}
			const char *msg, *l_msg;
			if ( challenges[ ichallenge ].authReplyMsg != AUTH_REPLY_PRINT ) {
				msg = authReplyMsg[ challenges[ ichallenge ].authReplyMsg ];
			} else {
				msg = challenges[ ichallenge ].authReplyPrint.c_str();
			}
			l_msg = common->Translate( msg );
			
			common->DPrintf( "%s: %s\n", Sys_NetAdrToString( from ), l_msg );

			if ( challenges[ ichallenge ].authReplyMsg == AUTH_REPLY_UNKNOWN || challenges[ ichallenge ].authReplyMsg == AUTH_REPLY_WAITING ) {
				// the client may be trying to connect to us in LAN mode, and the server disagrees
				// let the client know so it would switch to authed connection
				idBitMsg outMsg;
				byte msgBuf[ MAX_MESSAGE_SIZE ];
				outMsg.Init( msgBuf, sizeof( msgBuf ) );
				outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
				outMsg.WriteString( "authrequired" );
				serverPort.SendPacket( from, outMsg.GetData(), outMsg.GetSize() );
			}
		
			PrintOOB( from, SERVER_PRINT_MISC, msg );
		
			// update the guid in the challenges
			idStr::snPrintf( challenges[ ichallenge ].guid, sizeof( challenges[ ichallenge ].guid ), guid );

			// once auth replied denied, stop sending further requests
			if ( challenges[ ichallenge ].authReply != AUTH_DENY ) {
				// emit a cd key confirmation request
				outMsg.Init( msgBuf, sizeof( msgBuf ) );
				outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
				outMsg.WriteString( "srvAuth" );
				outMsg.WriteInt( ASYNC_PROTOCOL_VERSION );
				outMsg.WriteNetadr( from );
				outMsg.WriteInt( clientId );
				outMsg.WriteString( guid );	
				// protocol 1.37 addition
				// might expect : outMsg.WriteByte( fileSystem->RunningD3XP() );
				serverPort.SendPacket( idAsyncNetwork::GetMasterAddress(), outMsg.GetData(), outMsg.GetSize() );
			}
			return;
		default:
			assert( challenges[ ichallenge ].authState == CDK_OK );
	}

	numClients = 0;
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		serverClient_t &client = clients[ i ];
		if ( client.clientState >= SCS_CHILL ) {
			numClients++;
		}
	}

	// game may be passworded, client banned by IP or GUID
	// meanwhile, the max players may have been reached
	msg.ReadString( password, sizeof( password ) );
	char reason[MAX_STRING_CHARS];
	allowReply_t reply = game->ServerAllowClient( numClients, Sys_NetAdrToString( from ), guid, password, reason );
	if ( reply != ALLOW_YES ) {
		common->DPrintf( "game denied connection for %s\n", Sys_NetAdrToString( from ) );

		// SERVER_PRINT_GAMEDENY passes the game opcode through. Don't use PrintOOB
		outMsg.Init( msgBuf, sizeof( msgBuf ) );
		outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
		outMsg.WriteString( "print" );
		outMsg.WriteInt( SERVER_PRINT_GAMEDENY );
		outMsg.WriteInt( reply );
		outMsg.WriteString( reason );
		serverPort.SendPacket( from, outMsg.GetData(), outMsg.GetSize() );

		return;
	}

	ping = serverTime - challenges[ ichallenge ].pingTime;
	common->Printf( "challenge from %s connecting with %d ping\n", Sys_NetAdrToString( from ), ping );
	challenges[ ichallenge ].connected = true;

	// find a slot for the client
	for ( islot = 0; islot < 3; islot++ ) {
		for ( clientNum = 0; clientNum < MAX_ASYNC_CLIENTS; clientNum++ ) {
			serverClient_t &client = clients[ clientNum ];

			if ( islot == 0 ) {
				// if this slot uses the same IP and port
				if ( Sys_CompareNetAdrBase( from, client.channel.GetRemoteAddress() ) &&
						( clientId == client.clientId || from.port == client.channel.GetRemoteAddress().port ) ) {
					break;
				}
			} else if ( islot == 1 ) {
				// if this client is not connected and the slot uses the same IP
				if ( client.clientState >= SCS_CHILL ) {
					continue;
				}
				if ( Sys_CompareNetAdrBase( from, client.channel.GetRemoteAddress() ) ) {
					break;
				}
			} else if ( islot == 2 ) {
				// if this slot is free
				if ( client.clientState == SCS_FREE ) {
					break;
				}
			}
		}

		if ( clientNum < MAX_ASYNC_CLIENTS ) {
			// initialize
			clients[ clientNum ].channel.Init( from, serverId );
			clients[ clientNum ].OS = OS;
			strncpy( clients[ clientNum ].guid, guid, 12 );
			clients[ clientNum ].guid[11] = 0;
			break;
		}
	}

	// if no free spots available
	if ( clientNum >= MAX_ASYNC_CLIENTS ) {
		PrintOOB( from, SERVER_PRINT_MISC, "#str_04845" );
		return;
	}

	common->Printf( "sending connect response to %s\n", Sys_NetAdrToString( from ) );

	// send connect response message
	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
	outMsg.WriteString( "connectResponse" );
	outMsg.WriteInt( clientNum );
	outMsg.WriteInt( gameInitId );
	outMsg.WriteInt( gameFrame );
	outMsg.WriteInt( gameTime );
	outMsg.WriteDeltaDict( sessLocal.mapSpawnData.serverInfo, NULL );

	serverPort.SendPacket( from, outMsg.GetData(), outMsg.GetSize() );
	
	InitClient( clientNum, clientId, clientRate );

	clients[clientNum].gameInitSequence = 1;
	clients[clientNum].snapshotSequence = 1;

	// clear the challenge struct so a reconnect from this client IP starts clean
	memset( &challenges[ ichallenge ], 0, sizeof( challenge_t ) );
}

/*
==================
idAsyncServer::VerifyChecksumMessage
==================
*/
bool idAsyncServer::VerifyChecksumMessage( int clientNum, const netadr_t *from, const idBitMsg &msg, idStr &reply, int OS ) {
	int		i, numChecksums;
	int		checksums[ MAX_PURE_PAKS ];
	int		gamePakChecksum;
	int		serverChecksums[ MAX_PURE_PAKS ];

	// pak checksums, in a 0-terminated list
	numChecksums = 0;
	do {
		i = msg.ReadInt( );
		checksums[ numChecksums++ ] = i;
		// just to make sure a broken client doesn't crash us
		if ( numChecksums >= MAX_PURE_PAKS ) {
			common->Warning( "MAX_PURE_PAKS ( %d ) exceeded", MAX_PURE_PAKS );
			sprintf( reply, "#str_07144" );
			return false;
		}
	} while ( i );
	numChecksums--;

	// code pak checksum
	gamePakChecksum = msg.ReadInt( );
	// TODO: Check
	assert( serverChecksums[ 0 ] );

	// compare the lists
	for ( i = 0; serverChecksums[ i ] != 0; i++ ) {
		if ( checksums[ i ] != serverChecksums[ i ] ) {
			common->DPrintf( "client %s: pak missing ( 0x%x )\n", from ? Sys_NetAdrToString( *from ) : va( "%d", clientNum ), serverChecksums[ i ] );
			sprintf( reply, "pak missing ( 0x%x )\n", serverChecksums[ i ] );
			return false;
		}
	}
	if ( checksums[ i ] != 0 ) {
		common->DPrintf( "client %s: extra pak file referenced ( 0x%x )\n", from ? Sys_NetAdrToString( *from ) : va( "%d", clientNum ), checksums[ i ] );
		sprintf( reply, "extra pak file referenced ( 0x%x )\n", checksums[ i ] );
		return false;
	}
	return true;
}

/*
==================
idAsyncServer::RemoteConsoleOutput
==================
*/
void idAsyncServer::RemoteConsoleOutput( const char *string ) {
	noRconOutput = false;
	PrintOOB( rconAddress, SERVER_PRINT_RCON, string );
}

/*
==================
RConRedirect
==================
*/
void RConRedirect( const char *string ) {
	idAsyncNetwork::server.RemoteConsoleOutput( string );
}

/*
==================
idAsyncServer::ProcessRemoteConsoleMessage
==================
*/
void idAsyncServer::ProcessRemoteConsoleMessage( const netadr_t from, const idBitMsg &msg ) {
	idBitMsg	outMsg;
	byte		msgBuf[952];
	char		string[MAX_STRING_CHARS];

	if ( idAsyncNetwork::serverRemoteConsolePassword.GetString()[0] == '\0' ) {
		PrintOOB( from, SERVER_PRINT_MISC, "#str_04846" );
		return;
	}

	msg.ReadString( string, sizeof( string ) );

	if ( idStr::Icmp( string, idAsyncNetwork::serverRemoteConsolePassword.GetString() ) != 0 ) {
		PrintOOB( from, SERVER_PRINT_MISC, "#str_04847" );
		return;
	}

	msg.ReadString( string, sizeof( string ) );

	common->Printf( "rcon from %s: %s\n", Sys_NetAdrToString( from ), string );

	rconAddress = from;
	noRconOutput = true;
	common->BeginRedirect( (char *)msgBuf, sizeof( msgBuf ), RConRedirect );

	cmdSystem->BufferCommandText( CMD_EXEC_NOW, string );

	common->EndRedirect();

	if ( noRconOutput ) {
		PrintOOB( rconAddress, SERVER_PRINT_RCON, "#str_04848" );
	}
}

/*
==================
idAsyncServer::ProcessGetInfoMessage
==================
*/
void idAsyncServer::ProcessGetInfoMessage( const netadr_t from, const idBitMsg &msg ) {
	int			i, challenge;
	idBitMsg	outMsg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	if ( !IsActive() ) {
		return;
	}

	common->DPrintf( "Sending info response to %s\n", Sys_NetAdrToString( from ) );

	challenge = msg.ReadInt();

	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
	outMsg.WriteString( "infoResponse" );
	outMsg.WriteInt( challenge );
	outMsg.WriteInt( ASYNC_PROTOCOL_VERSION );
	outMsg.WriteDeltaDict( sessLocal.mapSpawnData.serverInfo, NULL );

	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		serverClient_t &client = clients[i];

		if ( client.clientState < SCS_CONNECTED ) {
			continue;
		}

		outMsg.WriteByte( i );
		outMsg.WriteShort( client.clientPing );
		outMsg.WriteInt( client.channel.GetMaxOutgoingRate() );
		outMsg.WriteString( sessLocal.mapSpawnData.userInfo[i].GetString( "ui_name", "Player" ) );
	}
	outMsg.WriteByte( MAX_ASYNC_CLIENTS );
	outMsg.WriteInt( fileSystem->GetOSMask() );

	serverPort.SendPacket( from, outMsg.GetData(), outMsg.GetSize() );
}

/*
===============
idAsyncServer::PrintLocalServerInfo
see (client) "getInfo" -> (server) "infoResponse" -> (client)ProcessGetInfoMessage
===============
*/
void idAsyncServer::PrintLocalServerInfo( void ) {
	int i;

	common->Printf( "server '%s' IP = %s\nprotocol %d.%d OS mask 0x%x\n", 
					sessLocal.mapSpawnData.serverInfo.GetString( "si_name" ),
					Sys_NetAdrToString( serverPort.GetAdr() ),
					ASYNC_PROTOCOL_MAJOR,
					ASYNC_PROTOCOL_MINOR,
					fileSystem->GetOSMask() );
	sessLocal.mapSpawnData.serverInfo.Print();
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		serverClient_t &client = clients[i];
		if ( client.clientState < SCS_CONNECTED ) {
			continue;
		}
		common->Printf( "client %2d: %s, ping = %d, rate = %d\n", i,
						sessLocal.mapSpawnData.userInfo[i].GetString( "ui_name", "Player" ),
						client.clientPing, client.channel.GetMaxOutgoingRate() );
	}
}

/*
==================
idAsyncServer::ConnectionlessMessage
==================
*/
bool idAsyncServer::ConnectionlessMessage( const netadr_t from, const idBitMsg &msg ) {
	char		string[MAX_STRING_CHARS*2];  // M. Quinn - Even Balance - PB Packets need more than 1024

	msg.ReadString( string, sizeof( string ) );

	// info request
	if ( idStr::Icmp( string, "getInfo" ) == 0 ) {
		ProcessGetInfoMessage( from, msg );
		return false;
	}

	// remote console
	if ( idStr::Icmp( string, "rcon" ) == 0 ) {
		ProcessRemoteConsoleMessage( from, msg );
		return true;
	}

	if ( !active ) {
		PrintOOB( from, SERVER_PRINT_MISC, "#str_04849" );
		return false;
	}

	// challenge from a client
	if ( idStr::Icmp( string, "challenge" ) == 0 ) {
		ProcessChallengeMessage( from, msg );
		return false;
	}

	// connect from a client
	if ( idStr::Icmp( string, "connect" ) == 0 ) {
		ProcessConnectMessage( from, msg );
		return false;
	}

	// download request
	if ( idStr::Icmp( string, "downloadRequest" ) == 0 ) {		
		ProcessDownloadRequestMessage( from, msg );
	}
	
	// auth server
	if ( idStr::Icmp( string, "auth" ) == 0 ) {
		if ( !Sys_CompareNetAdrBase( from, idAsyncNetwork::GetMasterAddress() ) ) {
			common->Printf( "auth: bad source %s\n", Sys_NetAdrToString( from ) );
			return false;
		}
		if ( idAsyncNetwork::LANServer.GetBool() ) {
			common->Printf( "auth message from master. net_LANServer is enabled, ignored.\n" );
		}
		ProcessAuthMessage( msg );
		return false;
	}

	return false;
}

/*
==================
idAsyncServer::ProcessMessage
==================
*/
bool idAsyncServer::ProcessMessage( const netadr_t from, idBitMsg &msg ) {
	int			i, id, sequence;
	idBitMsg	outMsg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	id = msg.ReadShort();

	// check for a connectionless message
	if ( id == CONNECTIONLESS_MESSAGE_ID ) {
		return ConnectionlessMessage( from, msg );
	}

	if ( msg.GetRemaingData() < 4 ) {
		common->DPrintf( "%s: tiny packet\n", Sys_NetAdrToString( from ) );
		return false;
	}

	// find out which client the message is from
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		serverClient_t &client = clients[i];

		if ( client.clientState == SCS_FREE ) {
			continue;
		}

		// This does not compare the UDP port, because some address translating
		// routers will change that at arbitrary times.
		if ( !Sys_CompareNetAdrBase( from, client.channel.GetRemoteAddress() ) || id != client.clientId ) {
			continue;
		}

		// make sure it is a valid, in sequence packet
		if ( !client.channel.Process( from, serverTime, msg, sequence ) ) {
			return false;		// out of order, duplicated, fragment, etc.
		}

		// zombie clients still need to do the channel processing to make sure they don't
		// need to retransmit the final reliable message, but they don't do any other processing
		if ( client.clientState == SCS_ZOMBIE ) {
			return false;
		}

		client.lastPacketTime = serverTime;

		ProcessReliableClientMessages( i );
		ProcessUnreliableClientMessage( i, msg );

		return false;
	}
	
	// if we received a sequenced packet from an address we don't recognize,
	// send an out of band disconnect packet to it
	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
	outMsg.WriteString( "disconnect" );
	serverPort.SendPacket( from, outMsg.GetData(), outMsg.GetSize() );

	return false;
}

/*
==================
idAsyncServer::SendReliableGameMessage
==================
*/
void idAsyncServer::SendReliableGameMessage( int clientNum, const idBitMsg &msg ) {
	int			i;
	idBitMsg	outMsg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteByte( SERVER_RELIABLE_MESSAGE_GAME );
	outMsg.WriteData( msg.GetData(), msg.GetSize() );

	if ( clientNum >= 0 && clientNum < MAX_ASYNC_CLIENTS ) {
		if ( clients[clientNum].clientState == SCS_INGAME ) {
			SendReliableMessage( clientNum, outMsg );
		}
		return;
	}

	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		if ( clients[i].clientState != SCS_INGAME ) {
			continue;
		}
		SendReliableMessage( i, outMsg );
	}
}

/*
==================
idAsyncServer::LocalClientSendReliableMessageExcluding
==================
*/
void idAsyncServer::SendReliableGameMessageExcluding( int clientNum, const idBitMsg &msg ) {
	int			i;
	idBitMsg	outMsg;
	byte		msgBuf[MAX_MESSAGE_SIZE];

	assert( clientNum >= 0 && clientNum < MAX_ASYNC_CLIENTS );

	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteByte( SERVER_RELIABLE_MESSAGE_GAME );
	outMsg.WriteData( msg.GetData(), msg.GetSize() );

	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		if ( i == clientNum ) {
			continue;
		}
		if ( clients[i].clientState != SCS_INGAME ) {
			continue;
		}
		SendReliableMessage( i, outMsg );
	}	
}

/*
==================
idAsyncServer::LocalClientSendReliableMessage
==================
*/
void idAsyncServer::LocalClientSendReliableMessage( const idBitMsg &msg ) {
	if ( localClientNum < 0 ) {
		common->Printf( "LocalClientSendReliableMessage: no local client\n" );
		return;
	}
	game->ServerProcessReliableMessage( localClientNum, msg );
}

/*
==================
idAsyncServer::ProcessConnectionLessMessages
==================
*/
void idAsyncServer::ProcessConnectionLessMessages( void ) {
	int			size, id;
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];
	netadr_t	from;

	if ( !serverPort.GetPort() ) {
		return;
	}

	while( serverPort.GetPacket( from, msgBuf, size, sizeof( msgBuf ) ) ) {
		msg.Init( msgBuf, sizeof( msgBuf ) );
		msg.SetSize( size );
		msg.BeginReading();
		id = msg.ReadShort();
		if ( id == CONNECTIONLESS_MESSAGE_ID ) {
			ConnectionlessMessage( from, msg );
		}
	}
}

/*
==================
idAsyncServer::UpdateTime
==================
*/
int idAsyncServer::UpdateTime( int clamp ) {
	int time, msec;

	time = Sys_Milliseconds();
	msec = idMath::ClampInt( 0, clamp, time - realTime );
	realTime = time;
	serverTime += msec;
	return msec;
}

/*
==================
idAsyncServer::RunFrame
==================
*/
void idAsyncServer::RunFrame( void ) {
	int			i, msec, size;
	bool		newPacket;
	idBitMsg	msg;
	byte		msgBuf[MAX_MESSAGE_SIZE];
	netadr_t	from;
	int			outgoingRate, incomingRate;
	float		outgoingCompression, incomingCompression;

	msec = UpdateTime( 100 );

	if ( !serverPort.GetPort() ) {
		return;
	}

	if ( !active ) {
		ProcessConnectionLessMessages();
		return;
	}
	
	gameTimeResidual += msec;

	// spin in place processing incoming packets until enough time lapsed to run a new game frame
	do {

		do {

			// blocking read with game time residual timeout
			newPacket = serverPort.GetPacketBlocking( from, msgBuf, size, sizeof( msgBuf ), USERCMD_MSEC - gameTimeResidual - 1 );
			if ( newPacket ) {
				msg.Init( msgBuf, sizeof( msgBuf ) );
				msg.SetSize( size );
				msg.BeginReading();
				if ( ProcessMessage( from, msg ) ) {
					return;	// return because rcon was used
				}
			}

			msec = UpdateTime( 100 );
			gameTimeResidual += msec;

		} while( newPacket );

	} while( gameTimeResidual < USERCMD_MSEC );

	// send heart beat to master servers
	MasterHeartbeat();

	// check for clients that timed out
	CheckClientTimeouts();

	if ( idAsyncNetwork::idleServer.GetBool() == ( !GetNumClients() || GetNumIdleClients() != GetNumClients()  ) ) {
		idAsyncNetwork::idleServer.SetBool( !idAsyncNetwork::idleServer.GetBool() );
		// the need to propagate right away, only this
		sessLocal.mapSpawnData.serverInfo.Set( "si_idleServer", idAsyncNetwork::idleServer.GetString() );
		game->SetServerInfo( sessLocal.mapSpawnData.serverInfo );
	}

	// make sure the time doesn't wrap
	if ( serverTime > 0x70000000 ) {
		ExecuteMapChange();
		return;
	}

	// check for synchronized cvar changes
	if ( cvarSystem->GetModifiedFlags() & CVAR_NETWORKSYNC ) {
		idDict newCvars;
		newCvars = *cvarSystem->MoveCVarsToDict( CVAR_NETWORKSYNC );
		SendSyncedCvarsBroadcast( newCvars );
		cvarSystem->ClearModifiedFlags( CVAR_NETWORKSYNC );
	}

	// check for user info changes of the local client
	if ( cvarSystem->GetModifiedFlags() & CVAR_USERINFO ) {
		if ( localClientNum >= 0 ) {
			idDict newInfo;
			game->ThrottleUserInfo( );
			newInfo = *cvarSystem->MoveCVarsToDict( CVAR_USERINFO );
			SendUserInfoBroadcast( localClientNum, newInfo );
		}
		cvarSystem->ClearModifiedFlags( CVAR_USERINFO );
	}

	// advance the server game
	while( gameTimeResidual >= USERCMD_MSEC ) {

		// sample input for the local client
		LocalClientInput();

		// duplicate usercmds for clients if no new ones are available
		DuplicateUsercmds( gameFrame, gameTime );

		// advance game
		gameReturn_t ret = game->RunFrame( userCmds[gameFrame & ( MAX_USERCMD_BACKUP - 1 ) ] );

		idAsyncNetwork::ExecuteSessionCommand( ret.sessionCommand );

		// update time
		gameFrame++;
		gameTime += USERCMD_MSEC;
		gameTimeResidual -= USERCMD_MSEC;
	}

	// duplicate usercmds so there is always at least one available to send with snapshots
	DuplicateUsercmds( gameFrame, gameTime );

	// send snapshots to connected clients
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		serverClient_t &client = clients[i];

		if ( client.clientState == SCS_FREE || i == localClientNum ) {
			continue;
		}

		// modify maximum rate if necesary
		if ( idAsyncNetwork::serverMaxClientRate.IsModified() ) {
			client.channel.SetMaxOutgoingRate( Min( client.clientRate, idAsyncNetwork::serverMaxClientRate.GetInteger() ) );
		}

		// if the channel is not yet ready to send new data
		if ( !client.channel.ReadyToSend( serverTime ) ) {
			continue;
		}

		// send additional message fragments if the last message was too large to send at once
		if ( client.channel.UnsentFragmentsLeft() ) {
			client.channel.SendNextFragment( serverPort, serverTime );
			continue;
		}

		if ( client.clientState == SCS_INGAME ) {
			if ( !SendSnapshotToClient( i ) ) {
				SendPingToClient( i );
			}
		} else {
			SendEmptyToClient( i );
		}
	}

	if ( com_showAsyncStats.GetBool() ) {

		UpdateAsyncStatsAvg();

		// dedicated will verbose to console
		if ( idAsyncNetwork::serverDedicated.GetBool() && serverTime >= nextAsyncStatsTime ) {
			common->Printf( "delay = %d msec, total outgoing rate = %d KB/s, total incoming rate = %d KB/s\n", GetDelay(), 
							GetOutgoingRate() >> 10, GetIncomingRate() >> 10 );

			for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {

				outgoingRate = GetClientOutgoingRate( i );
				incomingRate = GetClientIncomingRate( i );
				outgoingCompression = GetClientOutgoingCompression( i );
				incomingCompression = GetClientIncomingCompression( i );

				if ( outgoingRate != -1 && incomingRate != -1 ) {
					common->Printf( "client %d: out rate = %d B/s (% -2.1f%%), in rate = %d B/s (% -2.1f%%)\n",
									i, outgoingRate, outgoingCompression, incomingRate, incomingCompression );
				}
			}

			idStr msg;
			GetAsyncStatsAvgMsg( msg );
			common->Printf( va( "%s\n", msg.c_str() ) );

			nextAsyncStatsTime = serverTime + 1000;
		}
	}

	idAsyncNetwork::serverMaxClientRate.ClearModified();
}

/*
==================
idAsyncServer::PacifierUpdate
==================
*/
void idAsyncServer::PacifierUpdate( void ) {
	int i;

	if ( !IsActive() ) {
		return;
	}
	realTime = Sys_Milliseconds();
	ProcessConnectionLessMessages();
	for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ ) {
		if ( clients[i].clientState >= SCS_CHILL ) {
			if ( clients[i].channel.UnsentFragmentsLeft() ) {
				clients[i].channel.SendNextFragment( serverPort, serverTime );
			} else {
				SendEmptyToClient( i );
			}
		}
	}
}

/*
==================
idAsyncServer::PrintOOB
==================
*/
void idAsyncServer::PrintOOB( const netadr_t to, int opcode, const char *string ) {
	idBitMsg	outMsg;
	byte		msgBuf[ MAX_MESSAGE_SIZE ];

	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
	outMsg.WriteString( "print" );
	outMsg.WriteInt( opcode );
	outMsg.WriteString( string );
	serverPort.SendPacket( to, outMsg.GetData(), outMsg.GetSize() );
}

/*
==================
idAsyncServer::MasterHeartbeat
==================
*/
void idAsyncServer::MasterHeartbeat( bool force ) {
	if ( idAsyncNetwork::LANServer.GetBool() ) {
		if ( force ) {
			common->Printf( "net_LANServer is enabled. Not sending heartbeats\n" );
		}
		return;
	}
	if ( force ) {
		nextHeartbeatTime = 0;
	}
	// not yet
	if ( serverTime < nextHeartbeatTime ) {
		return;
	}
	nextHeartbeatTime = serverTime + HEARTBEAT_MSEC;
	for ( int i = 0 ; i < MAX_MASTER_SERVERS ; i++ ) {
		netadr_t adr;
		if ( idAsyncNetwork::GetMasterAddress( i, adr ) ) {
			common->Printf( "Sending heartbeat to %s\n", Sys_NetAdrToString( adr ) );
			idBitMsg outMsg;
			byte msgBuf[ MAX_MESSAGE_SIZE ];
			outMsg.Init( msgBuf, sizeof( msgBuf ) );
			outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
			outMsg.WriteString( "heartbeat" );
			serverPort.SendPacket( adr, outMsg.GetData(), outMsg.GetSize() );
		}
	}
}

/*
===============
idAsyncServer::SendEnterGameToClient
===============
*/
void idAsyncServer::SendEnterGameToClient( int clientNum ) {
	idBitMsg	msg;
	byte		msgBuf[ MAX_MESSAGE_SIZE ];

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteByte( SERVER_RELIABLE_MESSAGE_ENTERGAME );
	SendReliableMessage( clientNum, msg );
}

/*
===============
idAsyncServer::UpdateAsyncStatsAvg
===============
*/
void idAsyncServer::UpdateAsyncStatsAvg( void ) {
	stats_average_sum -= stats_outrate[ stats_current ];
	stats_outrate[ stats_current ] = idAsyncNetwork::server.GetOutgoingRate();
	if ( stats_outrate[ stats_current ] > stats_max ) {
		stats_max = stats_outrate[ stats_current ];
		stats_max_index = stats_current;
	} else if ( stats_current == stats_max_index ) {
		// find the new max
		int i;
		stats_max = 0;
		for ( i = 0; i < stats_numsamples ; i++ ) {
			if ( stats_outrate[ i ] > stats_max ) {
				stats_max = stats_outrate[ i ];
				stats_max_index = i;
			}
		}
	}
	stats_average_sum += stats_outrate[ stats_current ];
	stats_current++; stats_current %= stats_numsamples;
}

/*
===============
idAsyncServer::GetAsyncStatsAvgMsg
===============
*/
void idAsyncServer::GetAsyncStatsAvgMsg( idStr &msg ) {
	sprintf( msg, "avrg out: %d B/s - max %d B/s ( over %d ms )", stats_average_sum / stats_numsamples, stats_max, idAsyncNetwork::serverSnapshotDelay.GetInteger() * stats_numsamples );
}

/*
===============
idAsyncServer::ProcessDownloadRequestMessage
===============
*/
void idAsyncServer::ProcessDownloadRequestMessage( const netadr_t from, const idBitMsg &msg ) {
	int			challenge, clientId, iclient, numPaks, i;
	int			dlGamePak;
	int			dlPakChecksum;
	int			dlSize[ MAX_PURE_PAKS ];	// sizes
	idStrList	pakNames;					// relative path
	idStrList	pakURLs;					// game URLs
	char		pakbuf[ MAX_STRING_CHARS ];
	idStr		paklist;
	byte		msgBuf[ MAX_MESSAGE_SIZE ];
	byte		tmpBuf[ MAX_MESSAGE_SIZE ];
	idBitMsg	outMsg, tmpMsg;
	int			dlRequest;
	int			voidSlots = 0;				// to count and verbose the right number of paks requested for downloads

	challenge = msg.ReadInt();
	clientId = msg.ReadShort();
	dlRequest = msg.ReadInt();

	if ( ( iclient = ValidateChallenge( from, challenge, clientId ) ) == -1 ) {
		return;
	}
	
	// the first token of the pak names list passed to the game will be empty if no game pak is requested
	dlGamePak = msg.ReadInt();
	if ( dlGamePak ) {
		if ( !( dlSize[ 0 ] = fileSystem->ValidateDownloadPakForChecksum( dlGamePak, pakbuf, true ) ) ) {
			common->Warning( "client requested unknown game pak 0x%x", dlGamePak );
			pakbuf[ 0 ] = '\0';
			voidSlots++;
		}
	} else {
		pakbuf[ 0 ] = '\0';
		voidSlots++;
	}
	pakNames.Append( pakbuf );
	numPaks = 1;

	// read the checksums, build path names and pass that to the game code
	dlPakChecksum = msg.ReadInt();
	while ( dlPakChecksum ) {
		if ( !( dlSize[ numPaks ] = fileSystem->ValidateDownloadPakForChecksum( dlPakChecksum, pakbuf, false ) ) ) {
			// we pass an empty token to the game so our list doesn't get offset
			common->Warning( "client requested an unknown pak 0x%x", dlPakChecksum );
			pakbuf[ 0 ] = '\0';
			voidSlots++;
		}
		pakNames.Append( pakbuf );
		numPaks++;
		dlPakChecksum = msg.ReadInt();
	}

	for ( i = 0; i < pakNames.Num(); i++ ) {
		if ( i > 0 ) {
			paklist += ";";
		}
		paklist += pakNames[ i ].c_str();
	}

	// read the message and pass it to the game code
	common->DPrintf( "got download request for %d paks - %s\n", numPaks - voidSlots, paklist.c_str() );

	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteShort( CONNECTIONLESS_MESSAGE_ID );
	outMsg.WriteString( "downloadInfo" );
	outMsg.WriteInt( dlRequest );
	if ( !game->DownloadRequest( Sys_NetAdrToString( from ), challenges[ iclient ].guid, paklist.c_str(), pakbuf ) ) {
		common->DPrintf( "game: no downloads\n" );
		outMsg.WriteByte( SERVER_DL_NONE );
		serverPort.SendPacket( from, outMsg.GetData(), outMsg.GetSize() );
		return;
	}

	char *token, *next;
	int type = 0;

	token = pakbuf;
	next = strchr( token, ';' );
	while ( token ) {
		if ( next ) {
			*next = '\0';
		}
		
		if ( type == 0 ) {
			type = atoi( token );
		} else if ( type == SERVER_DL_REDIRECT ) {
			common->DPrintf( "download request: redirect to URL %s\n", token );
			outMsg.WriteByte( SERVER_DL_REDIRECT );
			outMsg.WriteString( token );
			serverPort.SendPacket( from, outMsg.GetData(), outMsg.GetSize() );
			return;
		} else if ( type == SERVER_DL_LIST ) {
			pakURLs.Append( token );
		} else {
			common->DPrintf( "wrong op type %d\n", type );
			next = token = NULL;
		}
		
		if ( next ) {
			token = next + 1;
			next = strchr( token, ';' );
		} else {
			token = NULL;
		}
	}

	if ( type == SERVER_DL_LIST ) {
		int totalDlSize = 0;
		int numActualPaks = 0;
		
		// put the answer packet together
		outMsg.WriteByte( SERVER_DL_LIST );			
		
		tmpMsg.Init( tmpBuf, MAX_MESSAGE_SIZE );

		for ( i = 0; i < pakURLs.Num(); i++ ) {
			tmpMsg.BeginWriting();
			if ( !dlSize[ i ] || !pakURLs[ i ].Length() ) {
				// still send the relative path so the client knows what it missed
				tmpMsg.WriteByte( SERVER_PAK_NO );
				tmpMsg.WriteString( pakNames[ i ] );
			} else {
				totalDlSize += dlSize[ i ];
				numActualPaks++;
				tmpMsg.WriteByte( SERVER_PAK_YES );
				tmpMsg.WriteString( pakNames[ i ] );
				tmpMsg.WriteString( pakURLs[ i ] );
				tmpMsg.WriteInt( dlSize[ i ] );
			}
			
			// keep last 5 bytes for an 'end of message' - SERVER_PAK_END and the totalDlSize long
			if ( outMsg.GetRemainingSpace() - tmpMsg.GetSize() > 5 ) {
				outMsg.WriteData( tmpMsg.GetData(), tmpMsg.GetSize() );
			} else {
				outMsg.WriteByte( SERVER_PAK_END );
				break;
			}
		}
		if ( i == pakURLs.Num() ) {
			// put a closure even if size not exceeded
			outMsg.WriteByte( SERVER_PAK_END );
		}
		common->DPrintf( "download request: download %d paks, %d bytes\n", numActualPaks, totalDlSize );

		serverPort.SendPacket( from, outMsg.GetData(), outMsg.GetSize() );
	}
}
