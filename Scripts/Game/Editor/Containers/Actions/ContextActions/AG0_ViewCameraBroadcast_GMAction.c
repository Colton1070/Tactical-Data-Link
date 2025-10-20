[BaseContainerProps(), SCR_BaseContainerCustomTitleUIInfo("m_Info")]
class AG0_GMViewPlayerCameraAction : SCR_BaseContextAction
{
    // Static state tracking
    protected static bool s_bViewingFeed = false;
    protected static AG0_TDLDeviceComponent s_ViewedDevice;
    protected static AG0_PlayerCameraOverrideComponent s_OverrideComponent;
    
    //------------------------------------------------------------------------------------------------
    override bool CanBeShown(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
    {
        if (!hoveredEntity) 
            return false;
            
        // Must be a player entity
        IEntity entity = hoveredEntity.GetOwner();
        if (!entity) 
            return false;
            
        // Check if it's a player character
        PlayerManager playerMgr = GetGame().GetPlayerManager();
        int playerId = playerMgr.GetPlayerIdFromControlledEntity(entity);
        if (playerId == 0) 
            return false;
            
        // Check if player has broadcasting TDL devices
        return HasBroadcastingTDLDevice(entity);
    }
    
    //------------------------------------------------------------------------------------------------
    override bool CanBePerformed(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
    {
        return CanBeShown(hoveredEntity, selectedEntities, cursorWorldPosition, flags);
    }
    
    //------------------------------------------------------------------------------------------------
    override void Perform(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags, int param = -1)
    {
        if (s_bViewingFeed) {
            ExitCameraView();
            return;
        }
        
        IEntity playerEntity = hoveredEntity.GetOwner();
        if (!playerEntity) return;
        
        AG0_TDLDeviceComponent broadcastingDevice = GetBroadcastingTDLDevice(playerEntity);
        if (!broadcastingDevice) {
            Print("AG0_GMViewPlayerCameraAction: No broadcasting device found", LogLevel.WARNING);
            return;
        }
        
        EnterCameraView(broadcastingDevice);
    }
    
    //------------------------------------------------------------------------------------------------
    protected bool HasBroadcastingTDLDevice(IEntity playerEntity)
    {
        return GetBroadcastingTDLDevice(playerEntity) != null;
    }
    
    //------------------------------------------------------------------------------------------------
    protected AG0_TDLDeviceComponent GetBroadcastingTDLDevice(IEntity playerEntity)
    {
        array<AG0_TDLDeviceComponent> playerDevices = GetPlayerTDLDevices(playerEntity);
        
        foreach (AG0_TDLDeviceComponent device : playerDevices) {
            if (device.IsCameraBroadcasting() && 
                device.HasCapability(AG0_ETDLDeviceCapability.VIDEO_SOURCE)) {
                return device;
            }
        }
        return null;
    }
    
    //------------------------------------------------------------------------------------------------
    // Local device discovery - replicates TDLSystem logic for client-side use
    protected array<AG0_TDLDeviceComponent> GetPlayerTDLDevices(IEntity playerEntity)
    {
        array<AG0_TDLDeviceComponent> devices = {};
        if (!playerEntity) return devices;
        
        // Check held gadgets
        SCR_GadgetManagerComponent gadgetMgr = SCR_GadgetManagerComponent.Cast(
            playerEntity.FindComponent(SCR_GadgetManagerComponent));
        if (gadgetMgr) {
            IEntity heldGadget = gadgetMgr.GetHeldGadget();
            if (heldGadget) {
                AG0_TDLDeviceComponent deviceComp = AG0_TDLDeviceComponent.Cast(
                    heldGadget.FindComponent(AG0_TDLDeviceComponent));
                if (deviceComp)
                    devices.Insert(deviceComp);
            }
        }
        
        // Check inventory
        InventoryStorageManagerComponent storage = InventoryStorageManagerComponent.Cast(
            playerEntity.FindComponent(InventoryStorageManagerComponent));
        if (storage) {
            array<IEntity> items = {};
            storage.GetItems(items);
            foreach (IEntity item : items) {
                AG0_TDLDeviceComponent deviceComp = AG0_TDLDeviceComponent.Cast(
                    item.FindComponent(AG0_TDLDeviceComponent));
                if (deviceComp)
                    devices.Insert(deviceComp);
            }
        }
        
        // Check equipment slots
        ChimeraCharacter character = ChimeraCharacter.Cast(playerEntity);
        if (character) {
            EquipedLoadoutStorageComponent loadoutStorage = 
                EquipedLoadoutStorageComponent.Cast(character.FindComponent(EquipedLoadoutStorageComponent));
            if (loadoutStorage) {
                array<typename> equipmentAreas = {
                    LoadoutHeadCoverArea, LoadoutArmoredVestSlotArea, 
                    LoadoutVestArea, LoadoutJacketArea, LoadoutBackpackArea
                };
                
                foreach (typename area : equipmentAreas) {
                    IEntity container = loadoutStorage.GetClothFromArea(area);
                    if (!container) continue;
                    
                    ClothNodeStorageComponent clothStorage = ClothNodeStorageComponent.Cast(
                        container.FindComponent(ClothNodeStorageComponent));
                    if (!clothStorage) continue;
                    
                    array<IEntity> clothItems = {};
                    clothStorage.GetAll(clothItems);
                    
                    foreach (IEntity clothItem : clothItems) {
                        AG0_TDLDeviceComponent deviceComp = AG0_TDLDeviceComponent.Cast(
                            clothItem.FindComponent(AG0_TDLDeviceComponent));
                        if (deviceComp)
                            devices.Insert(deviceComp);
                    }
                }
            }
        }
        
        return devices;
    }
    
    //------------------------------------------------------------------------------------------------
    protected void EnterCameraView(AG0_TDLDeviceComponent broadcasterDevice)
    {
        if (System.IsConsoleApp()) return;
        
        Print("AG0_GMViewPlayerCameraAction: Entering GM camera view", LogLevel.DEBUG);
        
        // Get the GM camera
        SCR_ManualCamera gmCamera = SCR_CameraEditorComponent.GetCameraInstance();
        if (!gmCamera) {
            Print("AG0_GMViewPlayerCameraAction: No GM camera found", LogLevel.ERROR);
            return;
        }
        
        // Find our override component
        s_OverrideComponent = AG0_PlayerCameraOverrideComponent.Cast(
            gmCamera.FindCameraComponent(AG0_PlayerCameraOverrideComponent));
        
        if (!s_OverrideComponent) {
            Print("AG0_GMViewPlayerCameraAction: No override component found in GM camera", LogLevel.ERROR);
            Print("AG0_GMViewPlayerCameraAction: Make sure AG0_PlayerCameraOverrideComponent is added to the GM camera prefab", LogLevel.ERROR);
            return;
        }
        
        // Configure the override
        s_OverrideComponent.SetViewedDevice(broadcasterDevice);
        s_ViewedDevice = broadcasterDevice;
        s_bViewingFeed = true;
        
        // TODO: Apply broadcaster's camera effects for authentic view
        // broadcasterDevice.ApplyBroadcastEffects(gmCamera.GetCameraIndex());
        
        Print("AG0_GMViewPlayerCameraAction: GM camera override activated", LogLevel.DEBUG);
    }
    
    //------------------------------------------------------------------------------------------------
    static void ExitCameraView()
    {
        if (!s_bViewingFeed) return;
        
        Print("AG0_GMViewPlayerCameraAction: Exiting GM camera view", LogLevel.DEBUG);
        
        // Disable the override component
        if (s_OverrideComponent) {
            s_OverrideComponent.SetViewedDevice(null);
        }
        
        // TODO: Clear camera effects when we start applying them
        // SCR_ManualCamera gmCamera = SCR_CameraEditorComponent.GetCameraInstance();
        // if (gmCamera) {
        //     BaseWorld world = GetGame().GetWorld();
        //     for (int i = 0; i < 20; i++) {
        //         world.SetCameraPostProcessEffect(gmCamera.GetCameraIndex(), i, PostProcessEffectType.None, "");
        //     }
        // }
        
        s_bViewingFeed = false;
        s_ViewedDevice = null;
        s_OverrideComponent = null;
    }
    
    //------------------------------------------------------------------------------------------------
    // Static helpers for external queries
    static bool IsViewingPlayerCamera()
    {
        return s_bViewingFeed;
    }
    
    //------------------------------------------------------------------------------------------------
    static AG0_TDLDeviceComponent GetCurrentViewedDevice()
    {
        return s_ViewedDevice;
    }
}