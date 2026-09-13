# Prints one annotation-shaped stage line (see CMakeLists.txt POST_BUILD).
# The text is passed with -DGATE_TEXT="...".
message("native/src/stage.txt(1): error C999: ${GATE_TEXT}")
