-- FlowState Configuration & About
-- Category: CloneTools

macroscript FlowState_Config
    category:"CloneTools"
    buttonText:"FlowState Config"
    tooltip:"FlowState Configuration"
(
    try(destroyDialog FlowState_ConfigDialog)catch()

    local iniPath = (getDir #plugcfg) + "\\FlowState.ini"

    -- Read raw ini lines (no sections — matches C++ reader)
    fn readFlag path key default:true = (
        local f = openFile path mode:"r"
        if f == undefined do return default
        local result = default
        while not eof f do (
            local ln = readLine f
            if findString ln (key + "=0") != undefined do result = false
            if findString ln (key + "=1") != undefined do result = true
        )
        close f
        result
    )

    -- Write config preserving XB1 and unknown lines
    fn writeConfig path pp ps ms subobj light = (
        -- Read existing lines, keep XB1 assignments and unknowns
        local keep = #()
        local f = openFile path mode:"r"
        if f != undefined do (
            while not eof f do (
                local ln = readLine f
                if findString ln "XB1:" == 1 do append keep ln
            )
            close f
        )
        -- Write fresh
        f = createFile path
        if f == undefined do return()
        if not pp do format "PowerParams=0\n" to:f
        if not ps do format "PowerShader=0\n" to:f
        if not ms do format "ModStack=0\n" to:f
        if subobj do format "SubObjToggles=1\n" to:f
        if light do format "LightTheme=1\n" to:f
        for ln in keep do format "%\n" ln to:f
        close f
    )

    rollout FlowState_ConfigDialog "FlowState Configuration" width:280 height:380
    (
        group "Modules"
        (
            checkbox chk_powerparams "PowerParams (XButton2)" checked:true
            checkbox chk_powershader "PowerShader (Shift+XButton2)" checked:true
            checkbox chk_modstack "ModStack Search (Ctrl+XButton2)" checked:true
        )

        group "Options"
        (
            checkbox chk_subobj "Show Sub-Object Toggles (V/E/B/F/El)" checked:false
            checkbox chk_lighttheme "Light Theme (Brushed Aluminium)" checked:false
        )

        group "Actions"
        (
            button btn_save "Save Configuration" width:240 height:28
            button btn_forget "Clear All Settings (Reset)" width:240 height:28
        )

        group "About"
        (
            label lbl_name "FlowState v1.1" align:#center
            label lbl_desc "Floating parameters under the cursor." align:#center
            label lbl_author "CloneTools" align:#center
            label lbl_blank ""
            label lbl_keys1 "XButton2: PowerParams panel" align:#left
            label lbl_keys2 "Shift+XButton2: PowerShader" align:#left
            label lbl_keys3 "Ctrl+XButton2: ModStack search" align:#left
            label lbl_keys4 "Shift+XButton1: Time slider" align:#left
            label lbl_keys5 "Ctrl+XButton1: Opacity slider" align:#left
            label lbl_keys6 "XButton1 on fav: Assign drag axis" align:#left
            label lbl_keys7 "Right-click param: Toggle favorite" align:#left
            label lbl_keys8 "Ctrl+click param: Hide param" align:#left
        )

        on FlowState_ConfigDialog open do
        (
            chk_powerparams.checked = readFlag iniPath "PowerParams"
            chk_powershader.checked = readFlag iniPath "PowerShader"
            chk_modstack.checked = readFlag iniPath "ModStack"
            chk_subobj.checked = readFlag iniPath "SubObjToggles" default:false
            chk_lighttheme.checked = readFlag iniPath "LightTheme" default:false
        )

        on btn_save pressed do
        (
            writeConfig iniPath chk_powerparams.checked chk_powershader.checked chk_modstack.checked chk_subobj.checked chk_lighttheme.checked
            messageBox "Configuration saved.\nRestart 3ds Max for changes to take effect." title:"FlowState"
        )

        on btn_forget pressed do
        (
            if queryBox "Clear all FlowState settings?\n\nThis will reset collapsed groups, hidden params, favorites, and XB1 assignments." title:"FlowState" then
            (
                try(deleteFile ((getDir #plugcfg) + "\\PowerParams.cfg"))catch()
                try(deleteFile ((getDir #plugcfg) + "\\PowerShader.cfg"))catch()
                try(deleteFile ((getDir #plugcfg) + "\\PowerShader_Pins.cfg"))catch()
                try(deleteFile iniPath)catch()
                messageBox "Settings cleared.\nRestart 3ds Max to apply." title:"FlowState"
            )
        )
    )

    createDialog FlowState_ConfigDialog style:#(#style_sysmenu, #style_toolwindow)
)

macroscript FlowState_About
    category:"CloneTools"
    buttonText:"FlowState About"
    tooltip:"About FlowState"
(
    messageBox "FlowState v1.1\n\nFloating parameters under the cursor.\nPowered by CloneTools.\n\nXButton2: PowerParams\nShift+XButton2: PowerShader\nCtrl+XButton2: ModStack\nShift+XButton1: Time slider\nCtrl+XButton1: Opacity slider\nXButton1: Assigned param drag" title:"About FlowState"
)
