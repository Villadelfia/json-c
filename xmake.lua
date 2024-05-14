add_rules("mode.debug", "mode.release")
set_languages("clatest", "c++latest")

target("json-c")
    set_kind("binary")
    add_files("src/*.c")
