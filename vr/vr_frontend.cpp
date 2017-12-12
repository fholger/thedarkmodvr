#include "precompiled.h"
#include "VrSupport.h"
#pragma once

/*
==================
idPlayerView::StereoView
==================
*/
void idPlayerView::StereoView( idUserInterface *hud, const renderView_t *view, const int eye ) {
	if( !view ) {
		return;
	}

	renderView_t eyeView = *view;

	vrSupport->AdjustViewWithCurrentHeadPose( eyeView, eye );

	if( g_skipViewEffects.GetBool() ) {
		SingleView( hud, &eyeView );
	} else {
		// greebo: For underwater effects, use the Doom3 Doubleview
		if( static_cast<idPhysics_Player*>( player->GetPlayerPhysics() )->GetWaterLevel() >= WATERLEVEL_HEAD ) {
			DoubleVision( hud, &eyeView, cv_tdm_underwater_blur.GetInteger() );
		}
		else {
			SingleView( hud, &eyeView, false );
		}

		// Bloom related - J.C.Denton
		/* Update  post-process */
		//this->m_postProcessManager.Update();

		ScreenFade();
	}
}
