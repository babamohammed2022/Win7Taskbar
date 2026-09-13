# Final gate: verifies the freshly built core DLL landed in dist/.
# The DLL path is passed with -DGATE_DLL="...".
if(EXISTS "${GATE_DLL}")
    message("native/src/stage.txt(1): warning C999: STAGE final gate dll present")
else()
    message("native/src/stage.txt(1): warning C999: STAGE final gate DLL MISSING")
endif()
