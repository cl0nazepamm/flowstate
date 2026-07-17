-- FlowState PowerParams multi-shape regression smoke.
-- Verifies the MAXScript contract used by the native PB1 fallback without
-- resetting the scene. Temporary nodes are removed and the prior selection is
-- restored even when an assertion fails.

(
    local oldSelection = selection as array
    local created = #()
    local failure = undefined

    fn check condition message = if not condition do throw message

    try (
        local a = Rectangle name:"__FlowState_MultiShape_A" length:5 width:10
        local b = Rectangle name:"__FlowState_MultiShape_B" length:200 width:30
        local other = Box name:"__FlowState_MultiShape_Other" length:40 width:40 height:10
        created = #(a, b, other)

        select #(a, b)

        -- Bare $.baseObject is invalid here because $ is an ObjectSet. The
        -- panel must explicitly use selection[1] as its schema/display anchor.
        local props = getPropNames selection[1].baseObject
        check ((findItem props #length) > 0) "Rectangle length was not discovered"
        check ((findItem props #width) > 0) "Rectangle width was not discovered"

        -- Absolute typing normalizes every compatible selected shape.
        for obj in selection where (
            try ((classof obj.baseObject) == (classof selection[1].baseObject)) catch false
        ) do setProperty obj.baseObject #length 75.0
        check (a.length == 75.0 and b.length == 75.0) "Absolute multi-edit did not reach both Rectangles"

        -- Relative scrubbing starts from each object's own current value.
        a.length = 5.0
        b.length = 200.0
        for obj in selection where (
            try ((classof obj.baseObject) == (classof selection[1].baseObject)) catch false
        ) do (
            local o = obj.baseObject
            local v = getProperty o #length
            local step = if (abs v) > 100 then 10.0 else if (abs v) > 10 then 1.0 else if (abs v) > 1 then 0.1 else 0.01
            setProperty o #length (v + step)
        )
        check (abs (a.length - 5.1) < 0.0001) "First Rectangle scrub used the wrong source value"
        check (abs (b.length - 210.0) < 0.0001) "Second Rectangle scrub used the wrong source value"

        -- A property name shared by another class must not leak across a mixed
        -- selection; this mirrors FindParam's class gate on the PB2/poly path.
        select #(a, other)
        local boxWidth = other.width
        for obj in selection where (
            try ((classof obj.baseObject) == (classof selection[1].baseObject)) catch false
        ) do setProperty obj.baseObject #width 99.0
        check (a.width == 99.0) "Compatible Rectangle was skipped"
        check (other.width == boxWidth) "Rectangle width leaked into a Box"

        format "FlowState PowerParams multi-shape smoke: PASS\n"
    ) catch (
        failure = getCurrentException()
    )

    for node in created where isValidNode node do delete node
    clearSelection()
    local validOldSelection = for node in oldSelection where isValidNode node collect node
    if validOldSelection.count > 0 do select validOldSelection

    if failure != undefined do throw failure
    true
)
