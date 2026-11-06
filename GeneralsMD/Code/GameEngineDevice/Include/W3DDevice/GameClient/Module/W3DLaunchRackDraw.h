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

// FILE: W3DLaunchRackDraw.h ////////////////////////////////////////////////////////////////////
// Desc:   Hypersonic @feature Aiming launchers of a launch facility (Generals Hypersonic D-031, D-039, D-042). A
//         W3DModelDraw that also turns one bone per launcher (LauncherBones, in launcher number order) by the turn
//         the facility's magazine module reports for that launcher (MunitionMagazineUpdate turns it toward the
//         target while its munition prepares to launch, and back to rest). Client-side only: it reads logic state
//         and changes nothing in the logic.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"

//-------------------------------------------------------------------------------------------------
class W3DLaunchRackDrawModuleData : public W3DModelDrawModuleData
{
public:
	std::vector<AsciiString>	m_launcherBones;		///< one bone per launcher, in launcher number order

	W3DLaunchRackDrawModuleData();
	virtual ~W3DLaunchRackDrawModuleData() override;
	static void buildFieldParse(MultiIniFieldParse& p);
};

//-------------------------------------------------------------------------------------------------
class W3DLaunchRackDraw : public W3DModelDraw
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( W3DLaunchRackDraw, "W3DLaunchRackDraw" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( W3DLaunchRackDraw, W3DLaunchRackDrawModuleData )

public:

	W3DLaunchRackDraw( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual void doDrawModule(const Matrix3D* transformMtx) override;

protected:

	virtual void onRenderObjRecreated() override;

private:

	struct LauncherInfo
	{
		Int			m_bone;				///< bone index in m_boneRenderObj (0 = not found)
		Real		m_angle;			///< turn away from the rest heading last applied
	};

	void aimLaunchers();

	std::vector<LauncherInfo>	m_launchers;
	RenderObjClass*						m_boneRenderObj;	///< render object the bone indices belong to
};
