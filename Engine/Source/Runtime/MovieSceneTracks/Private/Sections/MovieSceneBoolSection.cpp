// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "MovieSceneTracksPrivatePCH.h"
#include "MovieSceneBoolSection.h"


UMovieSceneBoolSection::UMovieSceneBoolSection( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
	, DefaultValue(false)
{
	SetIsInfinite(true);
}


bool UMovieSceneBoolSection::Eval( float Position ) const
{
	return !!BoolCurve.Evaluate(Position, DefaultValue ? 1 : 0);
}


void UMovieSceneBoolSection::MoveSection( float DeltaPosition, TSet<FKeyHandle>& KeyHandles )
{
	Super::MoveSection( DeltaPosition, KeyHandles );

	BoolCurve.ShiftCurve(DeltaPosition, KeyHandles);
}


void UMovieSceneBoolSection::DilateSection( float DilationFactor, float Origin, TSet<FKeyHandle>& KeyHandles )
{
	float StartTime = GetStartTime();
	float EndTime = GetEndTime();

	Super::DilateSection(DilationFactor, Origin, KeyHandles);
	
	BoolCurve.ScaleCurve(Origin, DilationFactor, KeyHandles);
}


void UMovieSceneBoolSection::GetKeyHandles(TSet<FKeyHandle>& KeyHandles) const
{
	for (auto It(BoolCurve.GetKeyHandleIterator()); It; ++It)
	{
		float Time = BoolCurve.GetKeyTime(It.Key());
		if (IsTimeWithinSection(Time))
		{
			KeyHandles.Add(It.Key());
		}
	}
}


void UMovieSceneBoolSection::AddKey( float Time, bool Value, FKeyParams KeyParams )
{
	Modify();

	if (BoolCurve.GetNumKeys() == 0 && !KeyParams.bAddKeyEvenIfUnchanged)
	{
		BoolCurve.SetDefaultValue(Value ? 1 : 0);
	}
	else
	{
		BoolCurve.UpdateOrAddKey(Time, Value ? 1 : 0);
	}
}


bool UMovieSceneBoolSection::NewKeyIsNewData(float Time, bool Value, FKeyParams KeyParams) const
{
	if ( BoolCurve.GetNumKeys() == 0 || (KeyParams.bAutoKeying && Eval(Time) != Value) )
	{
		return true;
	}

	return false;
}
