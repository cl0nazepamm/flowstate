(
    local failures = #()
    local tested = 0
    local skipped = 0

    fn makeLoopSubdivision =
    (
        local result = undefined
        try (result = execute "CloneLoop()") catch ()
        if result == undefined do
            try (result = execute "Clone_Loop()") catch ()
        result
    )

    fn checkNode label node =
    (
        local beforeMesh = snapshotAsMesh node
        local beforeFaces = beforeMesh.numfaces
        local loopMod = makeLoopSubdivision()

        if loopMod == undefined then
        (
            append failures (label + ": Loop Subdivision class was not loaded")
            delete node
            false
        )
        else
        (
            loopMod.iterations = 1
            addModifier node loopMod

            local afterMesh = snapshotAsMesh node
            local afterFaces = afterMesh.numfaces
            local passed = afterFaces > beforeFaces
            format "LOOP_SUBDIVISION_SMOKE|%|before=%|after=%|result=%\n" label beforeFaces afterFaces (if passed then "PASS" else "FAIL")

            if not passed do
                append failures (label + ": face count did not increase")

            tested += 1
            delete node
            passed
        )
    )

    fn checkExpression label expression =
    (
        local node = undefined
        try (node = execute expression) catch ()
        if node == undefined then
        (
            skipped += 1
            format "LOOP_SUBDIVISION_SMOKE|%|result=SKIP\n" label
            false
        )
        else
        (
            checkNode label node
        )
    )

    resetMaxFile #noPrompt

    checkExpression "Box" "Box length:20 width:20 height:20"
    checkExpression "Sphere" "Sphere radius:10 segs:16"
    checkExpression "Cylinder" "Cylinder radius:10 height:20 sides:16"
    checkExpression "Cone" "Cone radius1:10 radius2:3 height:20 sides:16"
    checkExpression "Torus" "Torus radius1:12 radius2:3 segs:16 sides:8"
    checkExpression "Teapot" "Teapot radius:10 segs:4"
    checkExpression "Plane" "Plane length:20 width:20 lengthsegs:3 widthsegs:3"
    checkExpression "GeoSphere" "GeoSphere radius:10 segs:3"
    checkExpression "Tube" "Tube radius1:10 radius2:6 height:20 sides:16"
    checkExpression "Pyramid" "Pyramid width:20 depth:20 height:20"
    checkExpression "Hedra" "Hedra radius:10"
    checkExpression "ChamferBox" "ChamferBox length:20 width:20 height:20 fillet:2"
    checkExpression "ChamferCyl" "ChamferCyl radius:10 height:20 fillet:2 sides:16"
    checkExpression "Capsule" "Capsule radius:6 height:20 sides:16"
    checkExpression "OilTank" "OilTank radius:8 height:20 sides:16"
    checkExpression "Spindle" "Spindle radius:8 height:20 sides:16"
    checkExpression "Gengon" "Gengon radius:10 height:20 sides:6"
    checkExpression "Prism" "Prism side1:15 side2:15 side3:15 height:20"

    local editableMeshNode = Box length:20 width:20 height:20
    convertToMesh editableMeshNode
    checkNode "EditableMesh" editableMeshNode

    local editablePolyNode = Box length:20 width:20 height:20
    convertTo editablePolyNode Editable_Poly
    checkNode "EditablePoly" editablePolyNode

    format "LOOP_SUBDIVISION_SMOKE_SUMMARY|tested=%|skipped=%|failed=%\n" tested skipped failures.count
    if failures.count > 0 do
    (
        for failure in failures do format "LOOP_SUBDIVISION_SMOKE_FAILURE|%\n" failure
        throw "Loop Subdivision primitive smoke test failed"
    )
)
