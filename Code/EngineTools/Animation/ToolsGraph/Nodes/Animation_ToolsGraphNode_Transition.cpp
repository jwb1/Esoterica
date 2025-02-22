#include "Animation_ToolsGraphNode_Transition.h"
#include "EngineTools/Animation/ToolsGraph/Animation_ToolsGraph_Compilation.h"
#include "EngineTools/Animation/ToolsGraph/Graphs/Animation_ToolsGraph_FlowGraph.h"
#include "EngineTools/PropertyGrid/PropertyGridTypeEditingRules.h"
#include "EngineTools/NodeGraph/NodeGraph_Style.h"

//-------------------------------------------------------------------------

namespace EE::Animation
{
    TransitionToolsNode::TransitionToolsNode()
        : ResultToolsNode()
    {
        CreateInputPin( "Condition", GraphValueType::Bool );
        CreateInputPin( "Duration Override", GraphValueType::Float );
        CreateInputPin( "Sync Event Override", GraphValueType::Float );
        CreateInputPin( "Start Bone Mask", GraphValueType::BoneMask );
        CreateInputPin( "Target Sync ID", GraphValueType::ID );
    }

    void TransitionToolsNode::DrawInfoText( NodeGraph::DrawContext const& ctx, NodeGraph::UserContext* pUserContext )
    {
        BeginDrawInternalRegion( ctx );

        //-------------------------------------------------------------------------

        ImGui::Text( "Duration: %.2fs", m_duration.ToFloat() );

        bool const isInstantTransition = ( m_duration == 0.0f );
        if ( !isInstantTransition )
        {
            if ( m_clampDurationToSource )
            {
                ImGui::Text( "Clamped To Source" );
            }

            switch ( m_rootMotionBlend )
            {
                case RootMotionBlendMode::Blend:
                ImGui::Text( "Blend Root Motion" );
                break;

                case RootMotionBlendMode::Additive:
                ImGui::Text( "Blend Root Motion (Additive)" );
                break;

                case RootMotionBlendMode::IgnoreSource:
                ImGui::Text( "Ignore Source Root Motion" );
                break;

                case RootMotionBlendMode::IgnoreTarget:
                ImGui::Text( "Ignore Target Root Motion" );
                break;
            }
        }

        //-------------------------------------------------------------------------

        switch ( m_timeMatchMode )
        {
            case TimeMatchMode::None:
            break;

            case TimeMatchMode::Synchronized:
            {
                ImGui::Text( "Synchronized" );
            }
            break;

            case TimeMatchMode::MatchSourceSyncEventIndex:
            {
                ImGui::Text( "Match Sync Idx" );
            }
            break;

            case TimeMatchMode::MatchSourceSyncEventIndexAndPercentage:
            {
                ImGui::Text( "Match Sync Idx and %%" );
            }
            break;

            case TimeMatchMode::MatchSyncEventID:
            {
                ImGui::Text( "Match Sync ID" );
            }
            break;

            case TimeMatchMode::MatchSyncEventIDAndPercentage:
            {
                ImGui::Text( "Match Sync ID and %%" );
            }
            break;

            case TimeMatchMode::MatchClosestSyncEventID:
            {
                ImGui::Text( "Match Closest Sync ID" );
            }
            break;

            case TimeMatchMode::MatchClosestSyncEventIDAndPercentage:
            {
                ImGui::Text( "Match Closest Sync ID and %%" );
            }
            break;

            case TimeMatchMode::MatchSourceSyncEventPercentage:
            {
                ImGui::Text( "Match Sync %% Only" );
            }
            break;
        }

        ImGui::Text( "Sync Offset: %.2f", m_syncEventOffset );

        //-------------------------------------------------------------------------

        if ( m_canBeForced )
        {
            ImGui::Text( "Forced" );
        }

        EndDrawInternalRegion( ctx );
    }

    Color TransitionToolsNode::GetTitleBarColor() const
    {
        return m_canBeForced ? Colors::Salmon : FlowToolsNode::GetTitleBarColor();
    }

    //-------------------------------------------------------------------------

    class TransitionEditingRules : public PG::TTypeEditingRules<TransitionToolsNode>
    {
        using PG::TTypeEditingRules<TransitionToolsNode>::TTypeEditingRules;

        virtual HiddenState IsHidden( StringID const& propertyID ) override
        {
            static StringID const boneMaskBlendPropertyID( "m_boneMaskBlendInTimePercentage" );
            if ( propertyID == boneMaskBlendPropertyID )
            {
                if( m_pTypeInstance->GetConnectedInputNode<FlowToolsNode>( 3 ) == nullptr )
                {
                    return HiddenState::Hidden;
                }
            }

            //-------------------------------------------------------------------------

            static StringID const clampPropertyID( "m_clampDurationToSource" );
            static StringID const easingPropertyID( "m_blendWeightEasing" );
            static StringID const rootMotionPropertyID( "m_rootMotionBlend" );

            if ( propertyID == clampPropertyID || propertyID == easingPropertyID || propertyID == boneMaskBlendPropertyID || propertyID == rootMotionPropertyID )
            {
                return ( m_pTypeInstance->m_duration <= 0.0f ) ? HiddenState::Hidden : HiddenState::Visible;
            }

            return HiddenState::Unhandled;
        }
    };

    EE_PROPERTY_GRID_EDITING_RULES( TransitionEditingRulesFactory, TransitionToolsNode, TransitionEditingRules );

    //-------------------------------------------------------------------------

    TransitionConduitToolsNode::TransitionConduitToolsNode()
        : NodeGraph::TransitionConduitNode()
    {
        CreateSecondaryGraph<FlowGraph>( GraphType::TransitionConduit );
    }

    TransitionConduitToolsNode::TransitionConduitToolsNode( NodeGraph::StateNode const* pStartState, NodeGraph::StateNode const* pEndState )
        : NodeGraph::TransitionConduitNode( pStartState, pEndState )
    {
        CreateSecondaryGraph<FlowGraph>( GraphType::TransitionConduit );
    }

    bool TransitionConduitToolsNode::HasTransitions() const
    {
        return !GetSecondaryGraph()->FindAllNodesOfType<TransitionToolsNode>().empty();
    }

    Color TransitionConduitToolsNode::GetConduitColor( NodeGraph::DrawContext const& ctx, NodeGraph::UserContext* pUserContext, TBitFlags<NodeGraph::NodeVisualState> visualState ) const
    {
        // Is this an blocked transition
        if ( visualState.HasNoFlagsSet() && !HasTransitions() )
        {
            return NodeGraph::Style::s_connectionColorInvalid;
        }

        // Is this transition active?
        auto pGraphNodeContext = static_cast<ToolsGraphUserContext*>( pUserContext );
        if ( pGraphNodeContext->HasDebugData() && isAnyChildActive )
        {
            return NodeGraph::Style::s_connectionColorValid;
        }

        //-------------------------------------------------------------------------

        return NodeGraph::TransitionConduitNode::GetConduitColor( ctx, pUserContext, visualState );
    }

    void TransitionConduitToolsNode::PreDrawUpdate( NodeGraph::UserContext* pUserContext )
    {
        isAnyChildActive = false;
        m_transitionProgress = 0.0f;

        //-------------------------------------------------------------------------

        auto pGraphNodeContext = static_cast<ToolsGraphUserContext*>( pUserContext );
        bool const isPreviewing = pGraphNodeContext->HasDebugData();
        if ( isPreviewing )
        {
            auto childTransitions = GetSecondaryGraph()->FindAllNodesOfType<TransitionToolsNode>();
            for ( auto pTransition : childTransitions )
            {
                auto const runtimeNodeIdx = pGraphNodeContext->GetRuntimeGraphNodeIndex( pTransition->GetID() );
                if ( runtimeNodeIdx != InvalidIndex && pGraphNodeContext->IsNodeActive( runtimeNodeIdx ) )
                {
                    float progress = 0.0f;
                    auto pTransitionNode = static_cast<TransitionNode const*>( pGraphNodeContext->GetNodeDebugInstance( runtimeNodeIdx ) );
                    if ( pTransitionNode->IsInitialized() )
                    {
                        progress = pTransitionNode->GetProgressPercentage();
                    }

                    m_transitionProgress = Percentage( Math::Max( progress, 0.001f ) );
                    isAnyChildActive = true;
                    break;
                }
            }
        }
    }
}