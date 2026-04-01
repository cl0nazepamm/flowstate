-- FlowState Config Installer
-- Drag into 3ds Max viewport to install.
(
    local srcDir = getFilenamePath (getSourceFileName())
    local mcrSrc = srcDir + "CloneTools-FlowState_Config.mcr"
    local mcrDst = (getDir #userMacros) + "\\CloneTools-FlowState_Config.mcr"

    if not (doesFileExist mcrSrc) do (
        messageBox "CloneTools-FlowState_Config.mcr not found next to this script." title:"FlowState"
        return()
    )

    copyFile mcrSrc mcrDst
    macros.load (getDir #userMacros) "CloneTools-FlowState_Config.mcr"

    rollout FlowStateInstallRollout "FlowState Installer" width:300 height:80
    (
        label lbl "Config macro installed!" pos:[80,15] align:#center
        label lbl2 "Customize UI > CloneTools > FlowState Config" pos:[30,40] align:#center
    )
    createDialog FlowStateInstallRollout
)
