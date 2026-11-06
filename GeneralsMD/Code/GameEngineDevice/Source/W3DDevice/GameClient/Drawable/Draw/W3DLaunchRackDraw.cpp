/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 Generals Hypersonic
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// FILE: W3DLaunchRackDraw.cpp //////////////////////////////////////////////////////////////////
// Desc:   Hypersonic @feature Aiming launchers of a launch facility (see W3DLaunchRackDraw.h).
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include <math.h>

#include "Common/GameCommon.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/MunitionMagazineUpdate.h"
#include "GameLogic/Object.h"
#include "W3DDevice/GameClient/Module/W3DLaunchRackDraw.h"
#include "WW3D2/rendobj.h"

//-------------------------------------------------------------------------------------------------
W3DLaunchRackDrawModuleData::W3DLaunchRackDrawModuleData()
{
}

//-------------------------------------------------------------------------------------------------
W3DLaunchRackDrawModuleData::~W3DLaunchRackDrawModuleData()
{
}

//-------------------------------------------------------------------------------------------------
void W3DLaunchRackDrawModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	W3DModelDrawModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "LauncherBones",		INI::parseAsciiStringVector,		nullptr, offsetof(W3DLaunchRackDrawModuleData, m_launcherBones) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DLaunchRackDraw::W3DLaunchRackDraw( Thing *thing, const ModuleData* moduleData )
: W3DModelDraw( thing, moduleData )
, m_boneRenderObj(nullptr)
{
	const W3DLaunchRackDrawModuleData *data = getW3DLaunchRackDrawModuleData();
	LauncherInfo info;
	info.m_bone = 0;
	info.m_angle = 0.0f;
	m_launchers.resize( data->m_launcherBones.size(), info );
}

//-------------------------------------------------------------------------------------------------
W3DLaunchRackDraw::~W3DLaunchRackDraw()
{
}

//-------------------------------------------------------------------------------------------------
void W3DLaunchRackDraw::onRenderObjRecreated()
{
	W3DModelDraw::onRenderObjRecreated();
	m_boneRenderObj = nullptr;	// look the bones up again in the new render object
}

//-------------------------------------------------------------------------------------------------
void W3DLaunchRackDraw::doDrawModule(const Matrix3D* transformMtx)
{
	W3DModelDraw::doDrawModule(transformMtx);
	aimLaunchers();
}

//-------------------------------------------------------------------------------------------------
// Turn each launcher bone by the turn its facility's logic reports (MunitionMagazineInterface::getLauncherTurn):
// toward the target while its munition prepares to launch, back to rest otherwise (D-042).
//-------------------------------------------------------------------------------------------------
void W3DLaunchRackDraw::aimLaunchers()
{
	const W3DLaunchRackDrawModuleData *data = getW3DLaunchRackDrawModuleData();
	RenderObjClass *robj = getRenderObject();
	const Object *obj = getDrawable()->getObject();
	if( robj == nullptr || obj == nullptr || m_launchers.empty() )
		return;

	if( robj != m_boneRenderObj )
	{
		for( size_t i = 0; i < m_launchers.size(); ++i )
			m_launchers[i].m_bone = robj->Get_Bone_Index( data->m_launcherBones[i].str() );
		m_boneRenderObj = robj;
	}

	const MunitionMagazineInterface *magazine = nullptr;
	for( BehaviorModule **m = obj->getBehaviorModules(); *m && magazine == nullptr; ++m )
	{
		const MunitionMagazineInterface *candidate = (*m)->getMunitionMagazineInterface();
		Real angle;
		if( candidate != nullptr && candidate->getLauncherTurn( 0, &angle ) )
			magazine = candidate;
	}

	for( size_t i = 0; i < m_launchers.size(); ++i )
	{
		Real angle = 0.0f;
		if( magazine == nullptr || !magazine->getLauncherTurn( (Int)i, &angle ) )
			angle = 0.0f;

		LauncherInfo &launcher = m_launchers[i];
		if( launcher.m_bone > 0 )
		{
			Matrix3D turn(1);
			turn.Rotate_Z( angle );
			robj->Capture_Bone( launcher.m_bone );
			robj->Control_Bone( launcher.m_bone, turn );
			launcher.m_angle = angle;
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void W3DLaunchRackDraw::crc( Xfer *xfer )
{

	// extend base class
	W3DModelDraw::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void W3DLaunchRackDraw::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	W3DModelDraw::xfer( xfer );

	// launcher angles are drawing state only; they settle again after a load

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void W3DLaunchRackDraw::loadPostProcess()
{

	// extend base class
	W3DModelDraw::loadPostProcess();

}
