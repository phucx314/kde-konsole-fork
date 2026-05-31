
kcoreaddons_add_plugin(konsole_vibecodingplugin
SOURCES
    vibecodingplugin.cpp
    filetreepanel.cpp
    gitpanel.cpp
INSTALL_NAMESPACE
    "konsoleplugins"
)

configure_file(konsole_vibecoding.in.json konsole_vibecoding.json)
target_link_libraries(konsole_vibecodingplugin
    konsoleprivate
    konsoleapp
)
