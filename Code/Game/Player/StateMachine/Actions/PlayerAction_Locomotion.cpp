#include "PlayerAction_Locomotion.h"
#include "Game/Player/Components/Component_MainPlayer.h"
#include "Game/Player/Physics/PlayerPhysicsController.h"
#include "Game/Player/Camera/PlayerCameraController.h"
#include "Game/Player/Animation/PlayerAnimationController.h"
#include "Game/Player/Animation/PlayerGraphController_Locomotion.h"
#include "Engine/Physics/Components/Component_PhysicsCharacter.h"
#include "System/Drawing/DebugDrawingSystem.h"
#include "System/Input/InputSystem.h"
#include "System/Math/MathHelpers.h"

//-------------------------------------------------------------------------

namespace EE::Player
{
    constexpr static float    g_maxSprintSpeed = 7.5f;                      // meters/second
    constexpr static float    g_maxRunSpeed = 5.0f;                         // meters/second
    constexpr static float    g_maxCrouchSpeed = 3.0f;                      // meters/second
    constexpr static float    g_timeToTriggerSprint = 1.5f;                 // seconds
    constexpr static float    g_timeToTriggerCrouch = 0.5f;                 // seconds
    constexpr static float    g_sprintStickAmplitude = 0.8f;                // [0,1]

    constexpr static float    g_idle_immediateStartThresholdAngle = Math::DegreesToRadians * 45.0f;
    constexpr static float    g_idle_minimumStickAmplitudeThreshold = 0.2f;
    constexpr static float    g_turnOnSpot_turnTime = 0.2f;
    constexpr static float    g_moving_detectStopTimer = 0.2f;
    constexpr static float    g_moving_detectTurnTimer = 0.2f;
    constexpr static float    g_stop_stopTime = 0.15f;

    //-------------------------------------------------------------------------

    static float ConvertStickAmplitudeToSpeed( ActionContext const& ctx, float stickAmplitude )
    {
        float const baseSpeed = ctx.m_pPlayerComponent->m_sprintFlag ? g_maxSprintSpeed : ctx.m_pPlayerComponent->m_crouchFlag ? g_maxCrouchSpeed : g_maxRunSpeed;
        return stickAmplitude * baseSpeed;
    }

    //-------------------------------------------------------------------------

    bool LocomotionAction::TryStartInternal( ActionContext const& ctx )
    {
        Transform const characterWorldTransform = ctx.m_pCharacterComponent->GetWorldTransform();
        Vector const& characterVelocity = ctx.m_pCharacterComponent->GetCharacterVelocity();
        float const horizontalSpeed = characterVelocity.GetLength2();

        ctx.m_pAnimationController->SetCharacterState( CharacterAnimationState::Locomotion );
        ctx.m_pCharacterController->EnableGravity( characterVelocity.m_z );
        ctx.m_pCharacterController->EnableProjectionOntoFloor();
        ctx.m_pCharacterController->EnableStepHeight();

        // Set initial state
        if ( horizontalSpeed > 0.1f )
        {
            RequestMoving( ctx, characterVelocity.Get2D() );
        }
        else
        {
            RequestIdle( ctx );
        }

        return true;
    }

    Action::Status LocomotionAction::UpdateInternal( ActionContext const& ctx )
    {
        auto const pControllerState = ctx.m_pInputState->GetControllerState();
        EE_ASSERT( pControllerState != nullptr );

        // Process inputs
        //-------------------------------------------------------------------------

        Vector const movementInputs = pControllerState->GetLeftAnalogStickValue();
        float const stickAmplitude = movementInputs.GetLength2();

        Vector const& camFwd = ctx.m_pCameraController->GetCameraRelativeForwardVector2D();
        Vector const& camRight = ctx.m_pCameraController->GetCameraRelativeRightVector2D();

        // Use last frame camera orientation
        Vector const stickDesiredForward = camFwd * movementInputs.m_y;
        Vector const stickDesiredRight = camRight * movementInputs.m_x;
        Vector const stickInputVectorWS = ( stickDesiredForward + stickDesiredRight );

        // Handle player state
        //-------------------------------------------------------------------------

        switch ( m_state )
        {
            case LocomotionState::Idle:
            {
                UpdateIdle( ctx, stickInputVectorWS, stickAmplitude );
            }
            break;

            case LocomotionState::TurningOnSpot:
            {
                UpdateTurnOnSpot( ctx, stickInputVectorWS, stickAmplitude );
            }
            break;

            case LocomotionState::Starting:
            {
                UpdateStarting( ctx, stickInputVectorWS, stickAmplitude );
            }
            break;

            case LocomotionState::Moving:
            {
                UpdateMoving( ctx, stickInputVectorWS, stickAmplitude );
            }
            break;

            case LocomotionState::PlantingAndTurning:
            {
                UpdateMoving( ctx, stickInputVectorWS, stickAmplitude );
            }
            break;

            case LocomotionState::Stopping:
            {
                UpdateStopping( ctx, stickInputVectorWS, stickAmplitude );
            }
            break;
        };

        // Handle unnavigable surfaces
        //-------------------------------------------------------------------------

        bool isSliding = false;

        if ( ctx.m_pCharacterController->GetFloorType() != CharacterPhysicsController::FloorType::Navigable && ctx.m_pCharacterComponent->GetCharacterVelocity().m_z < -Math::Epsilon )
        {
            m_slideTimer.Update( ctx.GetDeltaTime() );
            if ( m_slideTimer.GetElapsedTimeSeconds() > 0.35f )
            {
                isSliding = true;
                m_desiredFacing = ctx.m_pCharacterComponent->GetCharacterVelocity().GetNormalized2();
            }
        }
        else
        {
            m_slideTimer.Reset();
        }

        // Update animation controller
        //-------------------------------------------------------------------------

        auto pAnimController = ctx.GetAnimSubGraphController<LocomotionGraphController>();
        pAnimController->SetSliding( isSliding );
        pAnimController->SetCrouch( ctx.m_pPlayerComponent->m_crouchFlag );

        // Debug drawing
        //-------------------------------------------------------------------------

        #if EE_DEVELOPMENT_TOOLS
        if ( m_enableVisualizations )
        {
            Transform const characterWorldTransform = ctx.m_pCharacterComponent->GetWorldTransform();
            Vector const characterPosition = characterWorldTransform.GetTranslation();

            auto drawingCtx = ctx.GetDrawingContext();
            drawingCtx.DrawArrow( characterPosition, characterPosition + characterWorldTransform.GetForwardVector(), Colors::GreenYellow, 2.0f );
            drawingCtx.DrawArrow( characterPosition, characterPosition + stickInputVectorWS, Colors::White, 2.0f );
        }
        #endif

        return Status::Interruptible;
    }

    void LocomotionAction::StopInternal( ActionContext const& ctx, StopReason reason )
    {
        ctx.m_pPlayerComponent->m_sprintFlag = false;
        ctx.m_pPlayerComponent->m_crouchFlag = false;
    }

    //-------------------------------------------------------------------------

    void LocomotionAction::RequestIdle( ActionContext const& ctx )
    {
        Transform const characterWorldTransform = ctx.m_pCharacterComponent->GetWorldTransform();

        m_desiredHeading = Vector::Zero;
        m_cachedFacing = Vector::Zero;
        m_desiredTurnDirection = Vector::Zero;
        m_desiredFacing = characterWorldTransform.GetForwardVector();

        auto pAnimController = ctx.GetAnimSubGraphController<LocomotionGraphController>();
        pAnimController->RequestIdle();

        m_state = LocomotionState::Idle;
    }

    void LocomotionAction::UpdateIdle( ActionContext const& ctx, Vector const& stickInputVectorWS, float stickAmplitude )
    {
        EE_ASSERT( m_state == LocomotionState::Idle );

        //-------------------------------------------------------------------------

        auto const pControllerState = ctx.m_pInputState->GetControllerState();
        EE_ASSERT( pControllerState != nullptr );
        if ( pControllerState->WasPressed( Input::ControllerButton::FaceButtonLeft ) )
        {
            ctx.m_pPlayerComponent->m_crouchFlag = !ctx.m_pPlayerComponent->m_crouchFlag;
        }

        //-------------------------------------------------------------------------

        Transform const characterWorldTransform = ctx.m_pCharacterComponent->GetWorldTransform();
        Vector const characterForward = characterWorldTransform.GetForwardVector();

        m_desiredHeading = Vector::Zero;

        if ( stickAmplitude < g_idle_minimumStickAmplitudeThreshold )
        {
            auto pAnimController = ctx.GetAnimSubGraphController<LocomotionGraphController>();
            pAnimController->RequestIdle();
        }
        else // Handle stick input
        {
            Radians const deltaAngle = Math::GetAngleBetweenVectors( characterForward, stickInputVectorWS );
            if ( deltaAngle < g_idle_immediateStartThresholdAngle )
            {
                RequestStart( ctx, stickInputVectorWS, stickAmplitude );
            }
            else // turn on spot
            {
                RequestTurnOnSpot( ctx, stickInputVectorWS );
            }
        }
    }

    //-------------------------------------------------------------------------

    void LocomotionAction::RequestStart( ActionContext const& ctx, Vector const& stickInputVector, float stickAmplitude )
    {
        Transform const characterWorldTransform = ctx.m_pCharacterComponent->GetWorldTransform();

        m_desiredHeading = stickInputVector;
        m_cachedFacing = stickInputVector;
        m_desiredTurnDirection = Vector::Zero;
        m_desiredFacing = stickInputVector;

        float const speed = ConvertStickAmplitudeToSpeed( ctx, stickAmplitude );
        auto pAnimController = ctx.GetAnimSubGraphController<LocomotionGraphController>();
        pAnimController->RequestStart( stickInputVector * speed );

        m_state = LocomotionState::Starting;
    }

    void LocomotionAction::UpdateStarting( ActionContext const& ctx, Vector const& stickInputVector, float stickAmplitude )
    {
        EE_ASSERT( m_state == LocomotionState::Starting );

        auto pAnimController = ctx.GetAnimSubGraphController<LocomotionGraphController>();
        if ( pAnimController->IsMoving() )
        {
            float const speed = ConvertStickAmplitudeToSpeed( ctx, stickAmplitude );
            RequestMoving( ctx, stickInputVector * speed );
        }

        #if EE_DEVELOPMENT_TOOLS
        Transform const characterWorldTransform = ctx.m_pCharacterComponent->GetWorldTransform();
        Vector const characterPosition = characterWorldTransform.GetTranslation();
        auto drawingCtx = ctx.GetDrawingContext();
        InlineString const str( InlineString::CtorSprintf(), "Starting" );
        drawingCtx.DrawText3D( characterPosition + Vector( 0, 0, 1.0f ), str.c_str(), Colors::White, Drawing::FontSmall );
        drawingCtx.DrawArrow( characterPosition, characterPosition + m_desiredTurnDirection, Colors::Yellow, 3.0f );
        #endif
    }

    //-------------------------------------------------------------------------

    void LocomotionAction::RequestTurnOnSpot( ActionContext const& ctx, Vector const& desiredFacingDirection )
    {
        m_desiredHeading = Vector::Zero;
        m_cachedFacing = ctx.m_pCharacterComponent->GetForwardVector();
        m_desiredFacing = m_desiredTurnDirection = desiredFacingDirection.GetNormalized2();
        m_cachedFacing = Vector::Zero;

        auto pAnimController = ctx.GetAnimSubGraphController<LocomotionGraphController>();
        pAnimController->RequestTurnOnSpot( m_desiredFacing );

        m_state = LocomotionState::TurningOnSpot;
    }

    void LocomotionAction::UpdateTurnOnSpot( ActionContext const& ctx, Vector const& stickInputVectorWS, float stickAmplitude )
    {
        EE_ASSERT( m_state == LocomotionState::TurningOnSpot );

        #if EE_DEVELOPMENT_TOOLS
        Transform const characterWorldTransform = ctx.m_pCharacterComponent->GetWorldTransform();
        Vector const characterPosition = characterWorldTransform.GetTranslation();
        auto drawingCtx = ctx.GetDrawingContext();
        InlineString const str( InlineString::CtorSprintf(), "Turn On Spot" );
        drawingCtx.DrawText3D( characterPosition + Vector( 0, 0, 1.0f ), str.c_str(), Colors::White, Drawing::FontSmall );
        drawingCtx.DrawArrow( characterPosition, characterPosition + m_desiredTurnDirection, Colors::Orange, 3.0f );
        #endif

        auto pAnimController = ctx.GetAnimSubGraphController<LocomotionGraphController>();
        if ( pAnimController->IsTurningOnSpot() && pAnimController->IsAnyTransitionAllowed() )
        {
            RequestIdle( ctx );
        }
    }

    //-------------------------------------------------------------------------

    void LocomotionAction::RequestMoving( ActionContext const& ctx, Vector const& initialVelocity )
    {
        Transform const characterWorldTransform = ctx.m_pCharacterComponent->GetWorldTransform();

        m_desiredHeading = initialVelocity;
        m_cachedFacing = Vector::Zero;
        m_desiredTurnDirection = Vector::Zero;
        m_desiredFacing = m_desiredHeading.GetNormalized3();

        auto pAnimController = ctx.GetAnimSubGraphController<LocomotionGraphController>();
        pAnimController->RequestMove( ctx.GetDeltaTime(), m_desiredHeading, m_desiredFacing );

        m_state = LocomotionState::Moving;
    }

    void LocomotionAction::UpdateMoving( ActionContext const& ctx, Vector const& stickInputVectorWS, float stickAmplitude )
    {
        auto const pControllerState = ctx.m_pInputState->GetControllerState();
        EE_ASSERT( pControllerState != nullptr );

        auto pAnimController = ctx.GetAnimSubGraphController<LocomotionGraphController>();

        Transform const characterWorldTransform = ctx.m_pCharacterComponent->GetWorldTransform();

        //-------------------------------------------------------------------------

        if ( Math::IsNearZero( stickAmplitude ) )
        {
            // Check for stop
            // Start a stop timer but also keep the previous frames desires
            if ( m_generalTimer.IsRunning() )
            {
                #if EE_DEVELOPMENT_TOOLS
                auto drawingCtx = ctx.GetDrawingContext();
                float remainingTime = m_generalTimer.GetRemainingTime().ToFloat();
                InlineString const str( InlineString::CtorSprintf(), "Check for stop Timer: %.2fs left", remainingTime );
                drawingCtx.DrawText3D( characterWorldTransform.GetTranslation() + Vector( 0, 0, 1.0f ), str.c_str(), Colors::White, Drawing::FontSmall );
                #endif

                if ( m_generalTimer.Update( ctx.GetDeltaTime() ) )
                {
                    RequestStop( ctx );
                    return;
                }
            }
            else
            {
                m_generalTimer.Start( g_moving_detectStopTimer );
            }

            // Keep existing locomotion parameters when we have no input
            pAnimController->RequestMove( ctx.GetDeltaTime(), m_desiredHeading, m_desiredFacing );
        }
        else
        {
            // Clear the stop timer
            m_generalTimer.Stop();

            // Handle Sprinting
            //-------------------------------------------------------------------------

            if ( ctx.m_pPlayerComponent->m_sprintFlag )
            {
                if ( pControllerState->WasPressed( Input::ControllerButton::ThumbstickLeft ) )
                {
                    ctx.m_pPlayerComponent->m_sprintFlag = false;
                }
            }
            else // Not Sprinting
            {
                float const characterSpeed = ctx.m_pCharacterComponent->GetCharacterVelocity().GetLength2();
                if ( characterSpeed > 1.0f && pControllerState->WasPressed( Input::ControllerButton::ThumbstickLeft ) )
                {
                    ctx.m_pPlayerComponent->m_sprintFlag = true;
                    ctx.m_pPlayerComponent->m_crouchFlag = false;
                }

                if ( !ctx.m_pPlayerComponent->m_sprintFlag && pControllerState->WasPressed( Input::ControllerButton::FaceButtonLeft ) )
                {
                    ctx.m_pPlayerComponent->m_crouchFlag = !ctx.m_pPlayerComponent->m_crouchFlag;
                }
            }

            // Calculate desired heading and facing
            //-------------------------------------------------------------------------

            float const speed = ConvertStickAmplitudeToSpeed( ctx, stickAmplitude );
            float const maxAngularVelocity = Math::DegreesToRadians * ctx.m_pPlayerComponent->GetAngularVelocityLimit( speed );
            float const maxAngularDeltaThisFrame = maxAngularVelocity * ctx.GetDeltaTime();

            Vector const characterForward = characterWorldTransform.GetForwardVector();
            Radians const deltaAngle = Math::GetAngleBetweenVectors( characterForward, stickInputVectorWS );
            if ( Math::Abs( deltaAngle.ToFloat() ) > maxAngularDeltaThisFrame )
            {
                Radians rotationAngle = maxAngularDeltaThisFrame;
                if ( Math::IsVectorToTheRight2D( stickInputVectorWS, characterForward ) )
                {
                    rotationAngle = -rotationAngle;
                }

                Quaternion const rotation( AxisAngle( Vector::WorldUp, rotationAngle ) );
                m_desiredHeading = rotation.RotateVector( characterForward ) * speed;
            }
            else
            {
                m_desiredHeading = stickInputVectorWS * speed;
            }

            m_desiredFacing = m_desiredHeading.IsZero2() ? ctx.m_pCharacterComponent->GetForwardVector() : m_desiredHeading.GetNormalized2();

            pAnimController->RequestMove( ctx.GetDeltaTime(), m_desiredHeading, m_desiredFacing );
        }
    }

    //-------------------------------------------------------------------------

    void LocomotionAction::RequestStop( ActionContext const& ctx )
    {
        Transform const characterWorldTransform = ctx.m_pCharacterComponent->GetWorldTransform();

        m_desiredHeading = Vector::Zero;
        m_cachedFacing = Vector::Zero;
        m_desiredTurnDirection = Vector::Zero;
        m_desiredFacing = characterWorldTransform.GetForwardVector();

        auto pAnimController = ctx.GetAnimSubGraphController<LocomotionGraphController>();
        pAnimController->RequestStop( characterWorldTransform );

        m_state = LocomotionState::Stopping;
    }

    void LocomotionAction::UpdateStopping( ActionContext const& ctx, Vector const& stickInputVectorWS, float stickAmplitude )
    {
        auto pAnimController = ctx.GetAnimSubGraphController<LocomotionGraphController>();
        if ( pAnimController->IsIdle() )
        {
            RequestIdle( ctx );
        }
        else if ( stickAmplitude > 0.1f && pAnimController->IsAnyTransitionAllowed() )
        {
            // TODO: handle starting directly from here
            RequestIdle( ctx );
        }
        else
        {
            #if EE_DEVELOPMENT_TOOLS
            Transform const characterWorldTransform = ctx.m_pCharacterComponent->GetWorldTransform();
            auto drawingCtx = ctx.GetDrawingContext();
            drawingCtx.DrawText3D( characterWorldTransform.GetTranslation() + Vector( 0, 0, 1.0f ), "Stopping", Colors::White, Drawing::FontSmall);
            #endif
        }
    }

    //-------------------------------------------------------------------------

    #if EE_DEVELOPMENT_TOOLS
    void LocomotionAction::DrawDebugUI()
    {
        ImGui::Checkbox( "Enable Visualization", &m_enableVisualizations );
    }
    #endif
}