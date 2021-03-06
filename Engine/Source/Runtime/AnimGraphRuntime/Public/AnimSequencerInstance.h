// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/**
 *
 * This Instance only contains one AnimationAsset, and produce poses
 * Used by Preview in AnimGraph, Playing single animation in Kismet2 and etc
 */

#pragma once
#include "Animation/AnimInstance.h"
#include "AnimSequencerInstance.generated.h"

UCLASS(transient, NotBlueprintable)
class ANIMGRAPHRUNTIME_API UAnimSequencerInstance : public UAnimInstance
{
	GENERATED_UCLASS_BODY()

public:
	void UpdateAnimTrack(UAnimSequenceBase* InAnimSequence, int32 SequenceId, float InPosition, float Weight, bool bFireNotifies);
	void ResetNodes();
protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
};



