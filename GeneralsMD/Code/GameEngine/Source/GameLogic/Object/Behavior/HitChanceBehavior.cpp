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

// FILE: HitChanceBehavior.cpp //////////////////////////////////////////////////////////////////
// Desc:   Hypersonic @feature Interception hit chance of a drone or missile (see HitChanceBehavior.h).
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Player.h"
#include "Common/RandomValue.h"
#include "Common/Upgrade.h"
#include "Common/Xfer.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/HitChanceBehavior.h"

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
HitChanceBehaviorModuleData::HitChanceBehaviorModuleData()
{
	m_hitChance = 1.0f;
	m_upgradedHitChance = -1.0f;
	m_samHitIsLethal = FALSE;
	m_targetableByAntiAir = FALSE;
	m_immuneToPointDefense = FALSE;
	m_laserHitChance = -1.0f;
	m_upgradedLaserHitChance = -1.0f;
	m_secondUpgradedHitChance = -1.0f;
	m_secondUpgradedLaserHitChance = -1.0f;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void HitChanceBehaviorModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	UpgradeModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{
		{ "HitChance",						INI::parsePercentToReal,	nullptr, offsetof( HitChanceBehaviorModuleData, m_hitChance ) },
		{ "UpgradedHitChance",		INI::parsePercentToReal,	nullptr, offsetof( HitChanceBehaviorModuleData, m_upgradedHitChance ) },
		{ "SAMHitIsLethal",				INI::parseBool,						nullptr, offsetof( HitChanceBehaviorModuleData, m_samHitIsLethal ) },
		{ "TargetableByAntiAir",	INI::parseBool,						nullptr, offsetof( HitChanceBehaviorModuleData, m_targetableByAntiAir ) },
		{ "ImmuneToPointDefense",	INI::parseBool,						nullptr, offsetof( HitChanceBehaviorModuleData, m_immuneToPointDefense ) },
		{ "LaserHitChance",				INI::parsePercentToReal,	nullptr, offsetof( HitChanceBehaviorModuleData, m_laserHitChance ) },
		{ "UpgradedLaserHitChance",	INI::parsePercentToReal,	nullptr, offsetof( HitChanceBehaviorModuleData, m_upgradedLaserHitChance ) },
		{ "SecondUpgrade",				INI::parseAsciiString,		nullptr, offsetof( HitChanceBehaviorModuleData, m_secondUpgrade ) },	// Hypersonic (D-196)
		{ "SecondUpgradedHitChance",	INI::parsePercentToReal,	nullptr, offsetof( HitChanceBehaviorModuleData, m_secondUpgradedHitChance ) },
		{ "SecondUpgradedLaserHitChance",	INI::parsePercentToReal,	nullptr, offsetof( HitChanceBehaviorModuleData, m_secondUpgradedLaserHitChance ) },
		{ nullptr, nullptr, nullptr, 0 }
	};

	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature (D-196) Countermeasures I (the TriggeredBy upgrade) and II (SecondUpgrade): each level has its
// own chance; a level that leaves a chance negative falls back to the level below.
//-------------------------------------------------------------------------------------------------
Real HitChanceBehaviorModuleData::getChance( Bool pointDefenseLaser, Bool upgraded, Bool secondUpgraded ) const
{
	// D-059: a Point Defense Laser shot (weapon InterceptorClass = LASER) rolls its own chance when the object defines one
	if( pointDefenseLaser && m_laserHitChance >= 0.0f )
	{
		if( secondUpgraded && m_secondUpgradedLaserHitChance >= 0.0f )
			return m_secondUpgradedLaserHitChance;
		if( ( upgraded || secondUpgraded ) && m_upgradedLaserHitChance >= 0.0f )
			return m_upgradedLaserHitChance;
		return m_laserHitChance;
	}

	if( secondUpgraded && m_secondUpgradedHitChance >= 0.0f )
		return m_secondUpgradedHitChance;
	if( ( upgraded || secondUpgraded ) && m_upgradedHitChance >= 0.0f )
		return m_upgradedHitChance;
	return m_hitChance;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Bool HitChanceBehaviorModuleData::hasSecondUpgrade( const Player *player ) const
{
	if( player == nullptr || m_secondUpgrade.isEmpty() )
		return FALSE;
	const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade( m_secondUpgrade );
	return upgrade != nullptr && player->hasUpgradeComplete( upgrade );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UnsignedInt HitChanceBehavior::s_samHitLethalCount = 0;

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
HitChanceBehavior::HitChanceBehavior( Thing *thing, const ModuleData* moduleData ) : UpgradeModule( thing, moduleData )
{
	if( getHitChanceBehaviorModuleData()->m_samHitIsLethal )
		++s_samHitLethalCount;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
HitChanceBehavior::~HitChanceBehavior()
{
	if( getHitChanceBehaviorModuleData()->m_samHitIsLethal )
	{
		DEBUG_ASSERTCRASH( s_samHitLethalCount > 0, ("HitChanceBehavior SAMHitIsLethal count underflow") );
		--s_samHitLethalCount;
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
const HitChanceBehavior* HitChanceBehavior::findHitChanceBehavior( const Object *obj )
{
	if( obj == nullptr )
		return nullptr;

	static const NameKeyType key_HitChanceBehavior = NAMEKEY( "HitChanceBehavior" );
	for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
	{
		if( (*m)->getModuleNameKey() == key_HitChanceBehavior )
			return static_cast<const HitChanceBehavior*>( *m );
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Bool HitChanceBehavior::isActive() const
{
	// Drones sitting on their pads are hit normally; only flying drones and missiles roll.
	const Object *obj = getObject();
	return obj->isKindOf( KINDOF_PROJECTILE ) || obj->isAirborneTarget();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Real HitChanceBehavior::getHitChance( Bool pointDefenseLaser ) const
{
	const HitChanceBehaviorModuleData *data = getHitChanceBehaviorModuleData();

	// Hypersonic @feature (D-059) a Point Defense Laser shot rolls its own chance when the object defines one;
	// Countermeasures I (the TriggeredBy upgrade) and II (D-196, SecondUpgrade of the owner) apply to both chances.
	return data->getChance( pointDefenseLaser, isAlreadyUpgraded(), data->hasSecondUpgrade( getObject()->getControllingPlayer() ) );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Bool HitChanceBehavior::rollHit( Bool pointDefenseLaser, Real chanceScale ) const
{
	const Real chance = getHitChance( pointDefenseLaser ) * chanceScale;
	if( chance >= 1.0f )
		return TRUE;
	if( chance <= 0.0f )
		return FALSE;
	return GameLogicRandomValueReal( 0.0f, 1.0f ) < chance;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void HitChanceBehavior::crc( Xfer *xfer )
{

	// extend base class
	UpgradeModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void HitChanceBehavior::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpgradeModule::xfer( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void HitChanceBehavior::loadPostProcess()
{

	// extend base class
	UpgradeModule::loadPostProcess();

}
