# http://www.utf8everywhere.org/
add_definitions(
    /D_UNICODE=1
    /DUNICODE=1
)

add_library(libIME2_static STATIC
    # Core TSF part
    ImeModule.cpp
    ImeModule.h
    libIME.cpp
    libIME.h
    TextService.cpp
    TextService.h
    KeyEvent.cpp
    KeyEvent.h
    EditSession.cpp
    EditSession.h
    DisplayAttributeInfo.cpp
    DisplayAttributeInfo.h
    DisplayAttributeInfoEnum.cpp
    DisplayAttributeInfoEnum.h
    DisplayAttributeProvider.cpp
    DisplayAttributeProvider.h
    LangBarButton.cpp
    LangBarButton.h
    SinkAdvice.h
    Utils.cpp
    Utils.h
    ComPtr.h
    ComObject.h
    # GUI-related code
    DrawUtils.h
    DrawUtils.cpp
    Window.cpp
    Window.h
    ImeWindow.cpp
    ImeWindow.h
    MessageWindow.cpp
    MessageWindow.h
    CandidateWindow.h
    CandidateWindow.cpp
)

target_link_libraries(libIME2_static
    shlwapi.lib
)
