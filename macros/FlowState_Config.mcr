-- FlowState Configuration & About
-- Category: CloneTools

macroscript FlowState_Config
    category:"CloneTools"
    buttonText:"FlowState Config"
    tooltip:"FlowState Configuration"
(
    try(destroyDialog FlowState_ConfigDialog)catch()

    local iniPath = (getDir #plugcfg) + "\\FlowState.ini"

    rollout FlowState_ConfigDialog "FlowState Configuration" width:280 height:320
    (
        group "Modules"
        (
            checkbox chk_powerparams "PowerParams (Mouse4 Panel)" checked:true
            checkbox chk_powershader "PowerShader (Shift+Mouse4)" checked:true
            checkbox chk_modstack "ModStack Search (Ctrl+Mouse4)" checked:true
        )

        group "Settings"
        (
            button btn_save "Save Configuration" width:240 height:28
            button btn_forget "Clear All Settings (Reset)" width:240 height:28
        )

        group "About"
        (
            label lbl_name "FlowState" align:#center
            label lbl_ver "v1.0.0" align:#center
            label lbl_desc "Modifier stack under the cursor." align:#center
            label lbl_author "CloneTools" align:#center
            label lbl_blank ""
            label lbl_keys1 "Mouse4: PowerParams panel" align:#left
            label lbl_keys2 "Shift+Mouse4: PowerShader" align:#left
            label lbl_keys3 "Ctrl+Mouse4: ModStack search" align:#left
            label lbl_keys4 "Middle-click: Toggle side buttons" align:#left
            label lbl_keys5 "Right-click param: Toggle favorite" align:#left
            label lbl_keys6 "Ctrl+click param: Hide param" align:#left
            label lbl_keys7 "Shift+click header: Unhide params" align:#left
        )

        on FlowState_ConfigDialog open do
        (
            local v1 = getINISetting iniPath "FlowState" "PowerParams"
            local v2 = getINISetting iniPath "FlowState" "PowerShader"
            local v3 = getINISetting iniPath "FlowState" "ModStack"
            chk_powerparams.checked = (if v1 == "" then true else v1 == "1")
            chk_powershader.checked = (if v2 == "" then true else v2 == "1")
            chk_modstack.checked = (if v3 == "" then true else v3 == "1")
        )

        on btn_save pressed do
        (
            setINISetting iniPath "FlowState" "PowerParams" (if chk_powerparams.checked then "1" else "0")
            setINISetting iniPath "FlowState" "PowerShader" (if chk_powershader.checked then "1" else "0")
            setINISetting iniPath "FlowState" "ModStack" (if chk_modstack.checked then "1" else "0")
            messageBox "Configuration saved. Restart 3ds Max for changes to take effect." title:"FlowState"
        )

        on btn_forget pressed do
        (
            if queryBox "Clear all FlowState settings?\n\nThis will reset collapsed groups, hidden params, and favorites." title:"FlowState" then
            (
                local cfgPath = (getDir #plugcfg) + "\\PowerParams.cfg"
                try(deleteFile cfgPath)catch()
                try(deleteFile iniPath)catch()
                messageBox "Settings cleared. Restart 3ds Max to apply." title:"FlowState"
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
    messageBox "FlowState v1.0.0\n\nModifier stack under the cursor.\nPowered by CloneTools.\n\nMouse4: PowerParams\nShift+Mouse4: PowerShader\nCtrl+Mouse4: ModStack" title:"About FlowState"
)
