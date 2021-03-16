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

#ifndef __GAME_SECURITYCAMERA_H__
#define __GAME_SECURITYCAMERA_H__

/*
===================================================================================

	Security camera

===================================================================================
*/

// grayman #4615 - Refactored for 2.06

class idSecurityCamera : public idEntity {
public:
	CLASS_PROTOTYPE( idSecurityCamera );

	void					Spawn( void );

	void					Save( idSaveGame *savefile ) const;
	void					Restore( idRestoreGame *savefile );

	virtual void			Think( void );

	virtual renderView_t *	GetRenderView();
	virtual void			Killed( idEntity *inflictor, idEntity *attacker, int damage, const idVec3 &dir, int location );
	virtual bool			Pain( idEntity *inflictor, idEntity *attacker, int damage, const idVec3 &dir, int location );
	virtual	void			Damage(idEntity *inflictor, idEntity *attacker, const idVec3 &dir, const char *damageDefName,
														const float damageScale, const int location, trace_t *tr = NULL);
	virtual void			Present(void);

private:

	enum
	{
		MODE_SCANNING,
		MODE_LOSINGINTEREST,
		MODE_SIGHTED,
		MODE_ALERT
	};

	enum
	{
		STATE_SWEEPING,
		STATE_PLAYERSIGHTED,
		STATE_ALERTED,
		STATE_LOSTINTEREST,
		STATE_POWERRETURNS_SWEEPING,
		STATE_POWERRETURNS_PAUSED,
		STATE_PAUSED,
		STATE_DEAD
	};

	bool					rotate;
	bool					stationary;
	bool					sweeping;

	bool					negativeSweep;
	float					sweepAngle;
	float					sweepSpeed;
	float					sweepStartTime;
	float					sweepEndTime;
	float					percentSwept;

	bool					negativeIncline;
	float					inclineAngle;
	float					inclineSpeed;
	float					inclineStartTime;
	float					inclineEndTime;
	float					percentInclined;

	float					angle;
	float					angleTarget;
	float					anglePos1;
	float					anglePos2;
	float					angleToPlayer;

	float					incline;
	float					inclineTarget;
	float					inclinePos1;
	float					inclineToPlayer;

	bool					follow;
	bool					following;
	float					followSpeedMult;
	bool					followIncline;
	float					followTolerance;
	float					followInclineTolerance;

	float					constrainUp;
	float					constrainDown;
	float					constrainPositive;
	float					constrainNegative;

	float					scanDist;
	float					scanFov;
	float					scanFovCos;
	float					sightThreshold;

	int						modelAxis;
	bool					flipAxis;
	idVec3					viewOffset;

	int						pvsArea;
	idPhysics_RigidBody		physicsObj;
	idTraceModel			trm;

	idEntityPtr<idLight>	spotLight;
	idEntityPtr<idEntity>	sparks;
	idEntityPtr<idEntity>	cameraDisplay;

	int						state;
	int						alertMode;
	bool					powerOn;
	bool					spotlightPowerOn;
	bool					flinderized;

	float					timeLastSeen;
	float					lostInterestEndTime;
	float					nextAlertTime;
	float					startAlertTime;
	float					endAlertTime;
	float					alertDuration;
	bool					emitPauseSound;
	float					emitPauseSoundTime;
	float					pauseEndTime;
	float					nextSparkTime;

	bool					sparksOn;
	bool					sparksPowerDependent;
	bool					sparksPeriodic;
	float					sparksInterval;
	float					sparksIntervalRand;

	bool					useColors;
	idVec3					colorSweeping;
	idVec3					colorSighted;
	idVec3					colorAlerted;


	void					StartSweep( void );
	bool					CanSeePlayer( void );
	void					SetAlertMode( int status );
	void					DrawFov( void );
	const idVec3			GetAxis( void ) const;
	void					ReverseSweep( void );
	void					ContinueSweep( void );
	void					TurnToTarget( void );

	void					Event_AddLight( void );
	void					Event_AddSparks( void );
	void					Event_SpotLight_Toggle( void );
	void					Event_SpotLight_State( bool set );
	void					Event_Sweep_Toggle( void );
	void					Event_Sweep_State( bool set );
	void					Event_SeePlayer_Toggle( void );
	void					Event_SeePlayer_State( bool set );
	void					Event_GetSpotLight(void);
	void					Event_GetSecurityCameraState( void );
	void					Event_GetHealth( void );
	void					Event_SetHealth( float newHealth );
	void					Event_SetSightThreshold(float newThreshold);
	void					Event_On( void );
	void					Event_Off( void );

	void					PostSpawn( void );
	void					TriggerSparks( void );
	void					UpdateColors( void );

	void					Activate( idEntity* activator );
	float					GetCalibratedLightgemValue(idPlayer* player);
	bool					IsEntityHiddenByDarkness(idPlayer* player, const float sightThreshold);

};

#endif /* !__GAME_SECURITYCAMERA_H__ */
