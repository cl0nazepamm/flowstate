macroScript SmoothBridge
    category:"CloneTools"
    tooltip:"Smooth Bridge (G1/G0)"
    buttontext:"Smooth Bridge"
(
    rollout sbDlg "Smooth Bridge" width:200 height:172
    (
        spinner spnSegments "Segments:" pos:[10,10]  width:180 \
            range:[1,100,5]   type:#integer
        spinner spnTension  "Tension: " pos:[10,32]  width:180 \
            range:[0.0,3.0,1.0] type:#float scale:0.05

        groupBox grp "Border Continuity" pos:[6,58] width:188 height:50
        checkbox cbSmoothA "Smooth Side A" pos:[14,76] checked:true
        checkbox cbSmoothB "Smooth Side B" pos:[14,92] checked:true

        button btnApply  "Apply"  pos:[10,116] width:85 height:30
        button btnCancel "Cancel" pos:[105,116] width:85 height:30

        fn refresh = (
            try (
                SmoothBridge.preview spnSegments.value spnTension.value \
                    cbSmoothA.checked cbSmoothB.checked
            ) catch ()
        )

        on spnSegments changed val do refresh()
        on spnTension  changed val do refresh()
        on cbSmoothA   changed val do refresh()
        on cbSmoothB   changed val do refresh()

        on btnApply pressed do (
            SmoothBridge.commitPreview spnSegments.value spnTension.value \
                cbSmoothA.checked cbSmoothB.checked
            destroyDialog sbDlg
        )
        on btnCancel pressed do (
            SmoothBridge.cancelPreview()
            destroyDialog sbDlg
        )
        on sbDlg close do (
            -- treat window-close (X) as cancel
            SmoothBridge.cancelPreview()
        )
    )

    on execute do (
        if classof $ != Editable_Poly then (
            messagebox "Object must be Editable Poly. Right-click \xBB Convert To: Editable Poly." \
                title:"Smooth Bridge"
        ) else (
            SmoothBridge.beginPreview()
            SmoothBridge.preview 5 1.0 true true
            -- Modal: prevents touching the gizmo / sub-object tools during
            -- preview (which would crash because the in-progress preview
            -- mesh is in an inconsistent state vs EPoly's internal caches).
            -- Viewport orbit/pan/zoom still work via middle-mouse nav.
            createDialog sbDlg modal:true
        )
    )
)

macroScript CloneF2Extend
    category:"CloneTools"
    tooltip:"F2 Extend / Make Face"
    buttontext:"F2 Extend"
(
    on execute do (
        if classof $ == Editable_Poly do (
            local ok = false
            try (ok = F2Extend.extend()) catch (ok = false)
        )
    )
)
