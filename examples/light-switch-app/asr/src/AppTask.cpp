/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "AppTask.h"
#include "AppConfig.h"
#include "AppEvent.h"
#include "BindingHandler.h"
#include "ButtonHandler.h"
#include "CHIPDeviceManager.h"
#include "DeviceCallbacks.h"
#include "LEDManager.h"
#include "LightSwitch.h"
#include "init_Matter.h"
#include "lega_rtos_api.h"
#include <app/clusters/identify-server/identify-server.h>
#include <app/clusters/network-commissioning/network-commissioning.h>
#include <app/server/Dnssd.h>
#include <app/server/OnboardingCodesUtil.h>
#include <app/server/Server.h>
#include <app/util/attribute-storage.h>
#include <assert.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <lib/support/CodeUtils.h>
#include <platform/ASR/NetworkCommissioningDriver.h>
#include <platform/CHIPDeviceLayer.h>
#include <queue.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>

using namespace ::chip;
using namespace ::chip::Credentials;
using namespace ::chip::DeviceManager;
using namespace ::chip::DeviceLayer;
using namespace ::chip::System;

LightSwitch kLightSwitch1;

namespace {
TaskHandle_t sAppTaskHandle;
QueueHandle_t sAppEventQueue;

LEDManager sStatusLED;
LEDManager sSwitchLED;

constexpr EndpointId kLightSwitch1_EndpointId = 1;
#if DUAL_LIGHTSWITCH_ENDPOINT
LightSwitch kLightSwitch2;
constexpr EndpointId kLightSwitch2_EndpointId = 2;
#endif
#if GENERIC_SWITCH_ENDPOINT
LightSwitch GenericSwitch;
constexpr EndpointId kGenericSwitchEndpointId = 2;
#endif

constexpr size_t kAppEventQueueSize = 10;

constexpr EndpointId kNetworkCommissioningEndpointMain      = 0;
constexpr EndpointId kNetworkCommissioningEndpointSecondary = 0xFFFE;

app::Clusters::NetworkCommissioning::Instance
    sWiFiNetworkCommissioningInstance(kNetworkCommissioningEndpointMain /* Endpoint Id */,
                                      &(NetworkCommissioning::ASRWiFiDriver::GetInstance()));

void NetWorkCommissioningInstInit()
{
    sWiFiNetworkCommissioningInstance.Init();

    // We only have network commissioning on endpoint 0.
    emberAfEndpointEnableDisable(kNetworkCommissioningEndpointSecondary, false);
}

EmberAfIdentifyEffectIdentifier sIdentifyEffect = EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_STOP_EFFECT;

namespace {
void OnTriggerIdentifyEffectCompleted(chip::System::Layer * systemLayer, void * appState)
{
    sIdentifyEffect = EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_STOP_EFFECT;
}
} // namespace

void OnTriggerIdentifyEffect(Identify * identify)
{
    sIdentifyEffect = identify->mCurrentEffectIdentifier;

    if (identify->mCurrentEffectIdentifier == EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_CHANNEL_CHANGE)
    {
        ChipLogProgress(Zcl, "IDENTIFY_EFFECT_IDENTIFIER_CHANNEL_CHANGE - Not supported, use effect varriant %d",
                        identify->mEffectVariant);
        sIdentifyEffect = static_cast<EmberAfIdentifyEffectIdentifier>(identify->mEffectVariant);
    }

    switch (sIdentifyEffect)
    {
    case EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_BLINK:
    case EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_BREATHE:
    case EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_OKAY:
        (void) chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Seconds16(5), OnTriggerIdentifyEffectCompleted,
                                                           identify);
        break;
    case EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_FINISH_EFFECT:
        (void) chip::DeviceLayer::SystemLayer().CancelTimer(OnTriggerIdentifyEffectCompleted, identify);
        (void) chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Seconds16(1), OnTriggerIdentifyEffectCompleted,
                                                           identify);
        break;
    case EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_STOP_EFFECT:
        (void) chip::DeviceLayer::SystemLayer().CancelTimer(OnTriggerIdentifyEffectCompleted, identify);
        sIdentifyEffect = EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_STOP_EFFECT;
        break;
    default:
        ChipLogProgress(Zcl, "No identifier effect");
    }
}

Identify gIdentify = {
    chip::EndpointId{ 1 },
    [](Identify *) { ChipLogProgress(Zcl, "onIdentifyStart"); },
    [](Identify *) { ChipLogProgress(Zcl, "onIdentifyStop"); },
    EMBER_ZCL_IDENTIFY_IDENTIFY_TYPE_VISIBLE_LED,
    OnTriggerIdentifyEffect,
};
} // namespace

AppTask AppTask::sAppTask;
static DeviceCallbacks EchoCallbacks;

CHIP_ERROR AppTask::StartAppTask()
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    sAppEventQueue = xQueueCreate(APP_EVENT_QUEUE_SIZE, sizeof(AppEvent));
    if (sAppEventQueue == NULL)
    {
        ASR_LOG("Failed to allocate app event queue");
        appError(APP_ERROR_EVENT_QUEUE_FAILED);
    }

    // Start App task.
    xTaskCreate(AppTaskMain, TASK_NAME, APP_TASK_STACK_SIZE, 0, 2, &sAppTaskHandle);
    err = (sAppTaskHandle == nullptr) ? APP_ERROR_CREATE_TASK_FAILED : CHIP_NO_ERROR;

    return err;
}

CHIP_ERROR AppTask::Init()
{
    ASR_LOG("App Task started");

    if (MatterInitializer::Init_Matter_Stack(MATTER_DEVICE_NAME) != CHIP_NO_ERROR)
        return CHIP_ERROR_INTERNAL;

    CHIPDeviceManager & deviceMgr = CHIPDeviceManager::GetInstance();

    if (deviceMgr.Init(&EchoCallbacks) != CHIP_NO_ERROR)
        appError(CHIP_ERROR_INTERNAL);

    if (MatterInitializer::Init_Matter_Server() != CHIP_NO_ERROR)
        return CHIP_ERROR_INTERNAL;

    NetWorkCommissioningInstInit();

    kLightSwitch1.Init(kLightSwitch1_EndpointId);
#if DUAL_LIGHTSWITCH_ENDPOINT
    kLightSwitch2.Init(kLightSwitch2_EndpointId);
#endif
#if GENERIC_SWITCH_ENDPOINT
    GenericSwitch.InitGeneric(kGenericSwitchEndpointId);
#endif
    // Initialize LEDs
    LEDManager::InitGpio();

    sStatusLED.Init(SYSTEM_STATE_LED);
    sStatusLED.Set(false);

    sSwitchLED.Init(SWITCH_LED);
    sSwitchLED.Set(false);
    UpdateStatusLED();

    // Initialise WSTK buttons PB0 and PB1 (including debounce).
    ButtonHandler::Init();

    UpdateClusterState();

    ASR_LOG("Current Software Version: %s", CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING);

    ConfigurationMgr().LogDeviceConfig();

    // Print setup info
    PrintOnboardingCodes(chip::RendezvousInformationFlag(chip::RendezvousInformationFlag::kBLE));

    return CHIP_NO_ERROR;
}

void AppTask::AppTaskMain(void * pvParameter)
{
    AppEvent event;

    CHIP_ERROR err = sAppTask.Init();
    if (err != CHIP_NO_ERROR)
    {
        ASR_LOG("AppTask.Init() failed");
        appError(err);
    }

    while (true)
    {
        BaseType_t eventReceived = xQueueReceive(sAppEventQueue, &event, pdMS_TO_TICKS(10));
        while (eventReceived == pdTRUE)
        {
            sAppTask.DispatchEvent(&event);
            eventReceived = xQueueReceive(sAppEventQueue, &event, 0);
        }

        sStatusLED.Animate();
        sSwitchLED.Animate();
    }
}

void AppTask::PostEvent(const AppEvent * aEvent)
{
    if (sAppEventQueue != NULL)
    {
        BaseType_t status;
        if (lega_rtos_is_in_interrupt_context())
        {
            BaseType_t higherPrioTaskWoken = pdFALSE;
            status                         = xQueueSendFromISR(sAppEventQueue, aEvent, &higherPrioTaskWoken);

#ifdef portYIELD_FROM_ISR
            portYIELD_FROM_ISR(higherPrioTaskWoken);
#elif portEND_SWITCHING_ISR // portYIELD_FROM_ISR or portEND_SWITCHING_ISR
            portEND_SWITCHING_ISR(higherPrioTaskWoken);
#else                       // portYIELD_FROM_ISR or portEND_SWITCHING_ISR
#error "Must have portYIELD_FROM_ISR or portEND_SWITCHING_ISR"
#endif // portYIELD_FROM_ISR or portEND_SWITCHING_ISR
        }
        else
        {
            status = xQueueSend(sAppEventQueue, aEvent, 1);
        }

        if (!status)
            ASR_LOG("Failed to post event to app task event queue");
    }
    else
    {
        ASR_LOG("Event Queue is NULL should never happen");
    }
}

void AppTask::DispatchEvent(AppEvent * aEvent)
{
    if (aEvent->Handler)
    {
        aEvent->Handler(aEvent);
    }
    else
    {
        ASR_LOG("Event received with no handler. Dropping event.");
    }
}

void AppTask::UpdateClusterState(void) {}

#define SWITCH_Toggle

//#define SWITCH_DELAY_ONOFF

#if DUAL_LIGHTSWITCH_ENDPOINT
#undef SWITCH_Toggle
#undef SWITCH_DELAY_ONOFF
#endif

#ifdef SWITCH_DELAY_ONOFF
#define SWITCH_TIMER "switch_delay_timer"
#define SWITCH_DELAY_TIME 5 * 1000
static TimerHandle_t Switch_TimerHandle;
void Switch_Delay_CallBack(TimerHandle_t xTimer)
{
    ASR_LOG("Switch1 OFF!");
    kLightSwitch1.InitiateActionSwitch(LightSwitch::Action::Off);
}
#endif

void AppTask::ButtonPushHandler(AppEvent * aEvent)
{
    if (aEvent->Type == AppEvent::kEventType_Button)
    {
        switch (aEvent->ButtonEvent.PinNo)
        {
        case SWITCH1_BUTTON:
#ifdef SWITCH_Toggle
            // ASR_LOG("Switch1 Button Pushed.");
#endif
#if DUAL_LIGHTSWITCH_ENDPOINT
            ASR_LOG("Switch1 ON!");
            kLightSwitch1.InitiateActionSwitch(LightSwitch::Action::On);
#endif
#ifdef SWITCH_DELAY_ONOFF
            {
                ASR_LOG("Switch1 ON ,delay %d ms!", SWITCH_DELAY_TIME);
                kLightSwitch1.InitiateActionSwitch(LightSwitch::Action::On);

                if (Switch_TimerHandle == NULL)
                {
                    Switch_TimerHandle =
                        xTimerCreate(SWITCH_TIMER, pdMS_TO_TICKS(SWITCH_DELAY_TIME), pdFALSE, NULL, Switch_Delay_CallBack);
                    if (Switch_TimerHandle == NULL)
                    {
                        ASR_LOG("Failed to create a timer");
                    }
                }
                if (xTimerIsTimerActive(Switch_TimerHandle))
                {
                    ASR_LOG("switch timer already started!");
                    if (xTimerStop(Switch_TimerHandle, 0) == pdFAIL)
                    {
                        ASR_LOG("switch timer stop() failed");
                    }
                }
                if (xTimerChangePeriod(Switch_TimerHandle, pdMS_TO_TICKS(SWITCH_DELAY_TIME), 100) != pdPASS)
                {
                    ASR_LOG("switch timer start() failed");
                }
            }
#endif
            break;
#if DUAL_LIGHTSWITCH_ENDPOINT
        case SWITCH2_BUTTON:
            ASR_LOG("Switch2 ON!");
            kLightSwitch2.InitiateActionSwitch(LightSwitch::Action::On);
            break;
#endif
#if GENERIC_SWITCH_ENDPOINT
        case GENERIC_SWITCH_BUTTON:
            ASR_LOG("GenericSwitch: InitialPress");
            GenericSwitch.GenericSwitchInitialPress();
            break;
#endif
        default:
            break;
        }
    }
}

void AppTask::ButtonReleaseHandler(AppEvent * aEvent)
{

    if (aEvent->Type == AppEvent::kEventType_Button)
    {
        switch (aEvent->ButtonEvent.PinNo)
        {
        case SWITCH1_BUTTON:
#ifdef SWITCH_Toggle
            sSwitchLED.Set(false);
            ASR_LOG("Switch1 Toggle!");
            kLightSwitch1.InitiateActionSwitch(LightSwitch::Action::Toggle);
#endif
#if DUAL_LIGHTSWITCH_ENDPOINT
            ASR_LOG("Switch1 OFF!");
            kLightSwitch1.InitiateActionSwitch(LightSwitch::Action::Off);
#endif
            break;
#if DUAL_LIGHTSWITCH_ENDPOINT
        case SWITCH2_BUTTON:
            ASR_LOG("Switch2 OFF!");
            kLightSwitch2.InitiateActionSwitch(LightSwitch::Action::Off);
            break;
#endif
#if GENERIC_SWITCH_ENDPOINT
        case GENERIC_SWITCH_BUTTON:
            ASR_LOG("GenericSwitch: ShortRelease");
            GenericSwitch.GenericSwitchReleasePress();
            break;
#endif
        default:
            break;
        }
    }
}

void AppTask::StartBLEAdvertisingHandler(AppEvent * aEvent)
{
    /// Don't allow on starting Matter service BLE advertising after Thread provisioning.
    if (Server::GetInstance().GetFabricTable().FabricCount() != 0)
    {
        ASR_LOG("Matter service BLE advertising not started - device is already commissioned");
        return;
    }

    if (ConnectivityMgr().IsBLEAdvertisingEnabled())
    {
        ASR_LOG("BLE advertising is already enabled");
        return;
    }

    ASR_LOG("Enabling BLE advertising...");
    if (Server::GetInstance().GetCommissioningWindowManager().OpenBasicCommissioningWindow() != CHIP_NO_ERROR)
    {
        ASR_LOG("OpenBasicCommissioningWindow() failed");
    }
}

void AppTask::UpdateStatusLED()
{
    bool sHaveBLEConnections   = (ConnectivityMgr().NumBLEConnections() != 0);
    bool sIsWiFiBLEAdvertising = ConnectivityMgr().IsBLEAdvertisingEnabled();
    bool sIsWiFiProvisioned    = ConnectivityMgr().IsWiFiStationProvisioned();
    bool sIsWiFiEnabled        = ConnectivityMgr().IsWiFiStationEnabled();
    // Status LED indicates:
    // - blinking 1 s - advertising, ready to commission
    // - blinking 200 ms - commissioning in progress
    // - constant lightning means commissioned with Thread network
    if (sIsWiFiBLEAdvertising && !sHaveBLEConnections)
    {
        sStatusLED.Blink(50, 950);
    }
    else if (sIsWiFiProvisioned && sIsWiFiEnabled)
    {
        sStatusLED.Set(true);
    }
    else if (sHaveBLEConnections)
    {
        sStatusLED.Blink(30, 170);
    }
    else
    {
        sStatusLED.Blink(500);
    }
}

void AppTask::ButtonEventHandler(uint8_t btnIdx, uint8_t btnAction)
{

    AppEvent buttonEvent;
    buttonEvent.Type = AppEvent::kEventType_Button;

    if (btnIdx == SWITCH1_BUTTON)
    {
        if (btnAction == BUTTON_PRESSED)
        {
            buttonEvent.ButtonEvent.PinNo  = SWITCH1_BUTTON;
            buttonEvent.ButtonEvent.Action = AppEvent::kButtonPushEvent;
            buttonEvent.Handler            = ButtonPushHandler;
            sAppTask.PostEvent(&buttonEvent);
        }
        else if (btnAction == BUTTON_RELEASED)
        {
            buttonEvent.ButtonEvent.PinNo  = SWITCH1_BUTTON;
            buttonEvent.ButtonEvent.Action = AppEvent::kButtonReleaseEvent;
            buttonEvent.Handler            = ButtonReleaseHandler;
            sAppTask.PostEvent(&buttonEvent);
        }
    }
#if DUAL_LIGHTSWITCH_ENDPOINT
    else if (btnIdx == SWITCH2_BUTTON)
    {
        if (btnAction == BUTTON_PRESSED)
        {
            buttonEvent.ButtonEvent.PinNo  = SWITCH2_BUTTON;
            buttonEvent.ButtonEvent.Action = AppEvent::kButtonPushEvent;
            buttonEvent.Handler            = ButtonPushHandler;
            sAppTask.PostEvent(&buttonEvent);
        }
        else if (btnAction == BUTTON_RELEASED)
        {
            buttonEvent.ButtonEvent.PinNo  = SWITCH2_BUTTON;
            buttonEvent.ButtonEvent.Action = AppEvent::kButtonReleaseEvent;
            buttonEvent.Handler            = ButtonReleaseHandler;
            sAppTask.PostEvent(&buttonEvent);
        }
    }
#endif
#if GENERIC_SWITCH_ENDPOINT
    else if (btnIdx == GENERIC_SWITCH_BUTTON)
    {
        if (btnAction == BUTTON_PRESSED)
        {
            buttonEvent.ButtonEvent.PinNo  = GENERIC_SWITCH_BUTTON;
            buttonEvent.ButtonEvent.Action = AppEvent::kButtonPushEvent;
            buttonEvent.Handler            = ButtonPushHandler;
            sAppTask.PostEvent(&buttonEvent);
        }
        else if (btnAction == BUTTON_RELEASED)
        {
            buttonEvent.ButtonEvent.PinNo  = GENERIC_SWITCH_BUTTON;
            buttonEvent.ButtonEvent.Action = AppEvent::kButtonReleaseEvent;
            buttonEvent.Handler            = ButtonReleaseHandler;
            sAppTask.PostEvent(&buttonEvent);
        }
    }
#endif
}
