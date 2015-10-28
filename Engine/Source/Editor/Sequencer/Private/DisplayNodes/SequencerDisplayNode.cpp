// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "SequencerPrivatePCH.h"
#include "SequencerSectionLayoutBuilder.h"
#include "ISequencerSection.h"
#include "Sequencer.h"
#include "MovieScene.h"
#include "SSequencer.h"
#include "SSequencerSectionAreaView.h"
#include "MovieSceneSection.h"
#include "MovieSceneSequence.h"
#include "MovieSceneTrack.h"
#include "MovieSceneTrackEditor.h"
#include "CommonMovieSceneTools.h"
#include "IKeyArea.h"
#include "GroupedKeyArea.h"
#include "ObjectEditorUtils.h"

#define LOCTEXT_NAMESPACE "SequencerDisplayNode"

namespace SequencerNodeConstants
{
	extern const float CommonPadding;
	const float CommonPadding = 4.f;
}

class SSequencerObjectTrack : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSequencerObjectTrack) {}
		/** The view range of the section area */
		SLATE_ATTRIBUTE( TRange<float>, ViewRange )
	SLATE_END_ARGS()

	/** SLeafWidget Interface */
	virtual int32 OnPaint( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const override;
	virtual FVector2D ComputeDesiredSize(float) const override;
	
	void Construct( const FArguments& InArgs, TSharedRef<FSequencerDisplayNode> InRootNode )
	{
		RootNode = InRootNode;
		
		ViewRange = InArgs._ViewRange;

		check(RootNode->GetType() == ESequencerNode::Object);
	}

private:
	/** Collects all key times from the root node */
	void CollectAllKeyTimes(TArray<float>& OutKeyTimes) const;

	/** Adds a key time uniquely to an array of key times */
	void AddKeyTime(float NewTime, TArray<float>& OutKeyTimes) const;

private:
	/** Root node of this track view panel */
	TSharedPtr<FSequencerDisplayNode> RootNode;
	/** The current view range */
	TAttribute< TRange<float> > ViewRange;
};

int32 SSequencerObjectTrack::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	TArray<float> OutKeyTimes;
	CollectAllKeyTimes(OutKeyTimes);
	
	FTimeToPixel TimeToPixelConverter(AllottedGeometry, ViewRange.Get());

	for (int32 i = 0; i < OutKeyTimes.Num(); ++i)
	{
		float KeyPosition = TimeToPixelConverter.TimeToPixel(OutKeyTimes[i]);

		static const FVector2D KeyMarkSize = FVector2D(3.f, 21.f);
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId+1,
			AllottedGeometry.ToPaintGeometry(FVector2D(KeyPosition - FMath::CeilToFloat(KeyMarkSize.X/2.f), FMath::CeilToFloat(AllottedGeometry.Size.Y/2.f - KeyMarkSize.Y/2.f)), KeyMarkSize),
			FEditorStyle::GetBrush("Sequencer.KeyMark"),
			MyClippingRect,
			ESlateDrawEffect::None,
			FLinearColor(1.f, 1.f, 1.f, 1.f)
		);
	}

	return LayerId+1;
}

FVector2D SSequencerObjectTrack::ComputeDesiredSize( float ) const
{
	// Note: X Size is not used
	return FVector2D( 100.0f, RootNode->GetNodeHeight() );
}

void SSequencerObjectTrack::CollectAllKeyTimes(TArray<float>& OutKeyTimes) const
{
	TArray< TSharedRef<FSequencerSectionKeyAreaNode> > OutNodes;
	RootNode->GetChildKeyAreaNodesRecursively(OutNodes);

	for (int32 i = 0; i < OutNodes.Num(); ++i)
	{
		TArray< TSharedRef<IKeyArea> > KeyAreas = OutNodes[i]->GetAllKeyAreas();
		for (int32 j = 0; j < KeyAreas.Num(); ++j)
		{
			TArray<FKeyHandle> KeyHandles = KeyAreas[j]->GetUnsortedKeyHandles();
			for (int32 k = 0; k < KeyHandles.Num(); ++k)
			{
				AddKeyTime(KeyAreas[j]->GetKeyTime(KeyHandles[k]), OutKeyTimes);
			}
		}
	}
}

void SSequencerObjectTrack::AddKeyTime(float NewTime, TArray<float>& OutKeyTimes) const
{
	// @todo Sequencer It might be more efficient to add each key and do the pruning at the end
	for (int32 i = 0; i < OutKeyTimes.Num(); ++i)
	{
		if (FMath::IsNearlyEqual(OutKeyTimes[i], NewTime))
		{
			return;
		}
	}
	OutKeyTimes.Add(NewTime);
}



FSequencerDisplayNode::FSequencerDisplayNode( FName InNodeName, TSharedPtr<FSequencerDisplayNode> InParentNode, FSequencerNodeTree& InParentTree )
	: ParentNode( InParentNode )
	, ParentTree( InParentTree )
	, NodeName( InNodeName )
	, bExpanded( false )
{
	
}

void FSequencerDisplayNode::Initialize(float InVirtualTop, float InVirtualBottom)
{
	bExpanded = ParentTree.GetSavedExpansionState( *this );

	VirtualTop = InVirtualTop;
	VirtualBottom = InVirtualBottom;
}

void FSequencerDisplayNode::AddObjectBindingNode(TSharedRef<FSequencerObjectBindingNode> ObjectBindingNode)
{
	ChildNodes.Add(ObjectBindingNode);
}


bool FSequencerDisplayNode::Traverse_ChildFirst(const TFunctionRef<bool(FSequencerDisplayNode&)>& InPredicate, bool bIncludeThisNode)
{
	for (auto& Child : GetChildNodes())
	{
		if (!Child->Traverse_ChildFirst(InPredicate, true))
		{
			return false;
		}
	}

	return bIncludeThisNode ? InPredicate(*this) : true;
}

bool FSequencerDisplayNode::Traverse_ParentFirst(const TFunctionRef<bool(FSequencerDisplayNode&)>& InPredicate, bool bIncludeThisNode)
{
	if (bIncludeThisNode && !InPredicate(*this))
	{
		return false;
	}

	for (auto& Child : GetChildNodes())
	{
		if (!Child->Traverse_ParentFirst(InPredicate, true))
		{
			return false;
		}
	}

	return true;
}

bool FSequencerDisplayNode::TraverseVisible_ChildFirst(const TFunctionRef<bool(FSequencerDisplayNode&)>& InPredicate, bool bIncludeThisNode)
{
	// If the item is not expanded, its children ain't visible
	if (IsExpanded())
	{
		for (auto& Child : GetChildNodes())
		{
			if (!Child->IsHidden() && !Child->TraverseVisible_ChildFirst(InPredicate, true))
			{
				return false;
			}
		}
	}

	if (bIncludeThisNode && !IsHidden())
	{
		return InPredicate(*this);
	}

	// Continue iterating regardless of visibility
	return true;
}


bool FSequencerDisplayNode::TraverseVisible_ParentFirst(const TFunctionRef<bool(FSequencerDisplayNode&)>& InPredicate, bool bIncludeThisNode)
{
	if (bIncludeThisNode && !IsHidden() && !InPredicate(*this))
	{
		return false;
	}

	// If the item is not expanded, its children ain't visible
	if (IsExpanded())
	{
		for (auto& Child : GetChildNodes())
		{
			if (!Child->IsHidden() && !Child->TraverseVisible_ParentFirst(InPredicate, true))
			{
				return false;
			}
		}
	}

	return true;
}

TSharedRef<FSequencerSectionCategoryNode> FSequencerDisplayNode::AddCategoryNode( FName CategoryName, const FText& DisplayLabel )
{
	TSharedPtr<FSequencerSectionCategoryNode> CategoryNode;

	// See if there is an already existing category node to use
	for( int32 ChildIndex = 0; ChildIndex < ChildNodes.Num(); ++ChildIndex )
	{
		TSharedRef<FSequencerDisplayNode>& ChildNode = ChildNodes[ChildIndex];
		if( ChildNode->GetNodeName() == CategoryName && ChildNode->GetType() == ESequencerNode::Category )
		{
			CategoryNode = StaticCastSharedRef<FSequencerSectionCategoryNode>( ChildNode );
		}
	}

	if( !CategoryNode.IsValid() )
	{
		// No existing category found, make a new one
		CategoryNode = MakeShareable( new FSequencerSectionCategoryNode( CategoryName, DisplayLabel, SharedThis( this ), ParentTree ) );
		ChildNodes.Add( CategoryNode.ToSharedRef() );
	}

	return CategoryNode.ToSharedRef();
}

TSharedRef<FTrackNode> FSequencerDisplayNode::AddSectionAreaNode( FName SectionName, UMovieSceneTrack& AssociatedTrack, ISequencerTrackEditor& AssociatedEditor )
{
	TSharedPtr<FTrackNode> SectionNode;

	// See if there is an already existing section node to use
	for( int32 ChildIndex = 0; ChildIndex < ChildNodes.Num(); ++ChildIndex )
	{
		TSharedRef<FSequencerDisplayNode>& ChildNode = ChildNodes[ChildIndex];
		if( ChildNode->GetNodeName() == SectionName && ChildNode->GetType() == ESequencerNode::Track )
		{
			SectionNode = StaticCastSharedRef<FTrackNode>( ChildNode );
		}
	}

	if( !SectionNode.IsValid() )
	{
		// No existing node found make a new one
		SectionNode = MakeShareable( new FTrackNode( SectionName, AssociatedTrack, AssociatedEditor, SharedThis( this ), ParentTree ) );
		ChildNodes.Add( SectionNode.ToSharedRef() );
	}

	// The section node type has to match
	check( SectionNode->GetTrack() == &AssociatedTrack );

	return SectionNode.ToSharedRef();
}

void FSequencerDisplayNode::AddKeyAreaNode( FName KeyAreaName, const FText& DisplayName, TSharedRef<IKeyArea> KeyArea )
{
	TSharedPtr<FSequencerSectionKeyAreaNode> KeyAreaNode;

	// See if there is an already existing key area node to use
	for( int32 ChildIndex = 0; ChildIndex < ChildNodes.Num(); ++ChildIndex )
	{
		TSharedRef<FSequencerDisplayNode>& ChildNode = ChildNodes[ChildIndex];
		if( ChildNode->GetNodeName() == KeyAreaName && ChildNode->GetType() == ESequencerNode::KeyArea )
		{
			KeyAreaNode = StaticCastSharedRef<FSequencerSectionKeyAreaNode>( ChildNode );
		}
	}

	if( !KeyAreaNode.IsValid() )
	{
		// No existing node found make a new one
		KeyAreaNode = MakeShareable( new FSequencerSectionKeyAreaNode( KeyAreaName, DisplayName, SharedThis( this ), ParentTree ) );
		ChildNodes.Add( KeyAreaNode.ToSharedRef() );
	}

	KeyAreaNode->AddKeyArea( KeyArea );
}

TSharedRef<SWidget> FSequencerDisplayNode::GenerateContainerWidgetForOutliner(const TSharedRef<SSequencerTreeViewRow>& InRow)
{
	return SNew( SAnimationOutlinerTreeNode, SharedThis( this ), InRow );
}

TSharedRef<SWidget> FSequencerDisplayNode::GenerateEditWidgetForOutliner()
{
	return SNew(SSpacer);
}

TSharedRef<SWidget> FSequencerDisplayNode::GenerateWidgetForSectionArea( const TAttribute< TRange<float> >& ViewRange )
{
	if( GetType() == ESequencerNode::Track )
	{
		return 
			SNew( SSequencerSectionAreaView, SharedThis( this ) )
			.ViewRange( ViewRange );
	}
	else if (GetType() == ESequencerNode::Object)
	{
		return SNew(SSequencerObjectTrack, SharedThis(this))
			.ViewRange( ViewRange );
	}
	else
	{
		// Currently only section areas display widgets
		return SNullWidget::NullWidget;
	}
}

TSharedPtr<FSequencerDisplayNode> FSequencerDisplayNode::GetSectionAreaAuthority() const
{
	TSharedPtr<FSequencerDisplayNode> Authority = SharedThis(const_cast<FSequencerDisplayNode*>(this));

	while (Authority.IsValid())
	{
		if (Authority->GetType() == ESequencerNode::Object || Authority->GetType() == ESequencerNode::Track)
		{
			return Authority;
		}
		else
		{
			Authority = Authority->GetParent();
		}
	}
	return Authority;
}

FString FSequencerDisplayNode::GetPathName() const
{
	// First get our parent's path
	FString PathName;
	if( ParentNode.IsValid() )
	{
		PathName = ParentNode.Pin()->GetPathName() + TEXT(".");
	}

	//then append our path
	PathName += GetNodeName().ToString();

	return PathName;
}

TSharedPtr<SWidget> FSequencerDisplayNode::OnSummonContextMenu(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// @todo sequencer replace with UI Commands instead of faking it
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, NULL);

	BuildContextMenu(MenuBuilder);

	return MenuBuilder.MakeWidget();
}

void FSequencerDisplayNode::BuildContextMenu(FMenuBuilder& MenuBuilder)
{
	TSharedRef<FSequencerDisplayNode> NodeToBeDeleted = SharedThis(this);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("DeleteNode", "Delete"),
		LOCTEXT("DeleteNodeTooltip", "Delete this or selected nodes"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(&GetSequencer(), &FSequencer::DeleteNode, NodeToBeDeleted)));
}

void FSequencerDisplayNode::GetChildKeyAreaNodesRecursively(TArray< TSharedRef<FSequencerSectionKeyAreaNode> >& OutNodes) const
{
	for (int32 i = 0; i < ChildNodes.Num(); ++i)
	{
		if (ChildNodes[i]->GetType() == ESequencerNode::KeyArea)
		{
			OutNodes.Add(StaticCastSharedRef<FSequencerSectionKeyAreaNode>(ChildNodes[i]));
		}
		ChildNodes[i]->GetChildKeyAreaNodesRecursively(OutNodes);
	}
}

void FSequencerDisplayNode::SetExpansionState(bool bInExpanded)
{
	bExpanded = bInExpanded;

	// Expansion state has changed, save it to the movie scene now
	ParentTree.SaveExpansionState( *this, bExpanded );
}

bool FSequencerDisplayNode::IsExpanded() const
{
	return ParentTree.HasActiveFilter() ? true : bExpanded;
}

bool FSequencerDisplayNode::IsHidden() const
{
	return ParentTree.HasActiveFilter() && !ParentTree.IsNodeFiltered(AsShared());
}

TSharedRef<FGroupedKeyArea> FSequencerDisplayNode::GetKeyGrouping(int32 InSectionIndex)
{
	if (!KeyGroupings.IsValidIndex(InSectionIndex))
	{
		KeyGroupings.SetNum(InSectionIndex + 1);
	}

	if (!KeyGroupings[InSectionIndex].IsValid())
	{
		KeyGroupings[InSectionIndex] = MakeShareable(new FGroupedKeyArea(*this, InSectionIndex));
	}

	return KeyGroupings[InSectionIndex].ToSharedRef();
}

TSharedRef<FGroupedKeyArea> FSequencerDisplayNode::UpdateKeyGrouping(int32 InSectionIndex)
{
	if (!KeyGroupings.IsValidIndex(InSectionIndex))
	{
		KeyGroupings.SetNum(InSectionIndex + 1);
	}

	if (!KeyGroupings[InSectionIndex].IsValid())
	{
		KeyGroupings[InSectionIndex] = MakeShareable(new FGroupedKeyArea(*this, InSectionIndex));
	}
	else
	{
		*KeyGroupings[InSectionIndex] = FGroupedKeyArea(*this, InSectionIndex);
	}

	return KeyGroupings[InSectionIndex].ToSharedRef();
}


#undef LOCTEXT_NAMESPACE
