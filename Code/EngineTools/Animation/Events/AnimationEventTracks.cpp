#include "AnimationEventTracks.h"


//-------------------------------------------------------------------------

namespace EE::Animation
{
    TypeSystem::TypeInfo const* IDEventTrack::GetEventTypeInfo() const
    { 
        return IDEvent::s_pTypeInfo;
    }

    InlineString IDEventTrack::GetItemLabel( Timeline::TrackItem const* pItem ) const
    {
        auto pAnimEvent = GetAnimEvent<IDEvent>( pItem );
        return pAnimEvent->GetID().IsValid() ? pAnimEvent->GetID().c_str() : "Invalid ID";
    }

    //-------------------------------------------------------------------------

    TypeSystem::TypeInfo const* FootEventTrack::GetEventTypeInfo() const
    {
        return FootEvent::s_pTypeInfo;
    }

    InlineString FootEventTrack::GetItemLabel( Timeline::TrackItem const* pItem ) const
    {
        auto pAnimEvent = GetAnimEvent<FootEvent>( pItem );
        return FootEvent::GetPhaseName( pAnimEvent->GetFootPhase() );
    }

    Color FootEventTrack::GetItemColor( Timeline::TrackItem const * pItem ) const
    {
        auto pAnimEvent = GetAnimEvent<FootEvent>( pItem );
        return FootEvent::GetPhaseColor( pAnimEvent->GetFootPhase() );
    }

    //-------------------------------------------------------------------------

    TypeSystem::TypeInfo const* WarpEventTrack::GetEventTypeInfo() const
    {
        return WarpEvent::s_pTypeInfo;
    }

    InlineString WarpEventTrack::GetItemLabel( Timeline::TrackItem const * pItem ) const
    {
        auto pAnimEvent = GetAnimEvent<WarpEvent>( pItem );
        return pAnimEvent->GetDebugText();
    }

    //-------------------------------------------------------------------------

    TypeSystem::TypeInfo const* RagdollEventTrack::GetEventTypeInfo() const
    {
        return RagdollEvent::s_pTypeInfo;
    }

    InlineString RagdollEventTrack::GetItemLabel( Timeline::TrackItem const* pItem ) const
    {
        return "";
    }

    bool RagdollEventTrack::CanCreateNewItems() const
    {
        return GetNumItems() == 0;
    }

    ImRect RagdollEventTrack::DrawDurationItem( ImDrawList* pDrawList, Timeline::TrackItem* pItem, Float2 const& itemStartPos, Float2 const& itemEndPos, ItemState itemState )
    {
        constexpr static float const itemMarginY = 2;

        ImVec2 const adjustedItemStartPos = ImVec2( itemStartPos ) + ImVec2( 0, itemMarginY );
        ImVec2 const adjustedItemEndPos = ImVec2( itemEndPos ) - ImVec2( 0, itemMarginY );
        ImRect const itemRect( adjustedItemStartPos, adjustedItemEndPos );

        // Draw background
        //-------------------------------------------------------------------------

        ImVec2 const mousePos = ImGui::GetMousePos();
        bool const isHovered = itemRect.Contains( mousePos );
        pDrawList->AddRectFilled( adjustedItemStartPos, adjustedItemEndPos, GetItemBackgroundColor( itemState, isHovered ) );

        // Draw curve
        //-------------------------------------------------------------------------

        auto pRagdollEvent = GetAnimEvent<RagdollEvent>( pItem );
        FloatCurve const& curve = pRagdollEvent->m_physicsWeightCurve;

        if ( curve.GetNumPoints() > 0 )
        {
            constexpr float const lineWidth = 2.0f;
            float const curveCanvasWidth = itemRect.GetWidth();
            float const curveCanvasHeight = itemRect.GetHeight();

            pDrawList->PushClipRect( itemRect.Min, itemRect.Max );
            if ( curve.GetNumPoints() == 1 )
            {
                float const value = curve.GetPoint( 0 ).m_value;
                float const linePosY = adjustedItemEndPos.y - ( value * curveCanvasHeight );
                pDrawList->AddLine( ImVec2( adjustedItemStartPos.x, linePosY ), ImVec2( adjustedItemEndPos.x, linePosY ), ImGuiX::ConvertColor( Colors::HotPink ), lineWidth );

                if ( isHovered )
                {
                    pDrawList->AddLine( ImVec2( mousePos.x, adjustedItemStartPos.y ), ImVec2( mousePos.x, adjustedItemEndPos.y ), ImGuiX::ConvertColor( Colors::LightGray ), 1.0f );
                    pDrawList->AddCircleFilled( ImVec2( mousePos.x, linePosY ), 3.0f, ImGuiX::ConvertColor( Colors::LimeGreen ) );
                    ImGui::SetTooltip( " %.2f ", value );
                }
            }
            else
            {
                FloatRange const horizontalRange = curve.GetParameterRange();
                FloatRange const verticalRange = curve.GetValueRange();

                int32_t const numPointsToDraw = Math::RoundToInt( curveCanvasWidth / 2 ) + 1;
                float const stepT = 1.0f / ( numPointsToDraw - 1 );

                TVector<ImVec2> curvePoints;
                for ( auto i = 0; i < numPointsToDraw; i++ )
                {
                    float const t = ( i * stepT );
                    Float2 curvePoint;
                    curvePoint.m_x = itemRect.GetTL().x + ( t * curveCanvasWidth );
                    curvePoint.m_y = adjustedItemEndPos.y - ( curve.Evaluate( t ) * curveCanvasHeight );
                    curvePoints.emplace_back( curvePoint );
                }

                pDrawList->AddPolyline( curvePoints.data(), numPointsToDraw, ImGuiX::ConvertColor( Colors::HotPink ), 0, lineWidth);

                if ( isHovered )
                {
                    Percentage const percentageThroughRange( ( mousePos.x - adjustedItemStartPos.x ) / itemRect.GetWidth() );
                    float const value = curve.Evaluate( percentageThroughRange );
                    float const valuePixelY = adjustedItemEndPos.y - ( value * curveCanvasHeight );

                    pDrawList->AddLine( ImVec2( mousePos.x, adjustedItemStartPos.y ), ImVec2( mousePos.x, adjustedItemEndPos.y ), ImGuiX::ConvertColor( Colors::LightGray ), 1.0f );
                    pDrawList->AddCircleFilled( ImVec2( mousePos.x, valuePixelY ), 3.0f, ImGuiX::ConvertColor( Colors::LimeGreen ) );
                    ImGui::SetTooltip( " %.2f ", value );
                }
            }
            pDrawList->PopClipRect();
        }

        //-------------------------------------------------------------------------

        return itemRect;
    }

    Timeline::Track::Status RagdollEventTrack::GetValidationStatus() const
    {
        int32_t const numItems = GetNumItems();
        if ( numItems == 1 )
        {
            FloatRange const validRange( 0, 1 );
            auto pRagdollEvent = GetAnimEvent<RagdollEvent>( m_items[0] );
            auto const& curve = pRagdollEvent->m_physicsWeightCurve;
            if ( curve.GetNumPoints() == 0 )
            {
                m_statusMessage = "Curve has no data points!";
                return Timeline::Track::Status::HasWarnings;
            }
            else if ( curve.GetNumPoints() == 1 )
            {
                if ( !validRange.ContainsInclusive( curve.GetPoint( 0 ).m_value ) )
                {
                    m_statusMessage = "Curve values are outside valid range! Keep the curve between 0 and 1 on both axes!";
                    return Timeline::Track::Status::HasErrors;
                }
            }
            else // Check that the value range is valid!
            {
                FloatRange const valueRange = curve.GetValueRange();
                if ( !validRange.ContainsInclusive( valueRange.m_begin ) || !validRange.ContainsInclusive( valueRange.m_end ) )
                {
                    m_statusMessage = "Curve values are outside valid range! Keep the curve between 0 and 1 on both axes!";
                    return Timeline::Track::Status::HasErrors;
                }
            }
        }
        else if( numItems > 1 )
        {
            m_statusMessage = "More than one event detected! This is not allowed!";
            return Timeline::Track::Status::HasErrors;
        }

        //-------------------------------------------------------------------------

        ResetStatusMessage();
        return Timeline::Track::Status::Valid;
    }
}