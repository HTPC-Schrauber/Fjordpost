include(ECMAddTests)

ecm_add_tests(
    databasetest.cpp
    messagelistmodeltest.cpp
    mimetest.cpp
    autoconfigtest.cpp
    accountsetuptest.cpp
    smtptest.cpp
    pop3test.cpp
    m2featurestest.cpp
    pimtest.cpp
    davtest.cpp
    LINK_LIBRARIES fjordpostcore Qt6::Test
)

# Arch/CachyOS liefern die Testbibliothek von KIMAP nicht mit.
if(TARGET KPim6::kimaptest6)
    ecm_add_test(imapsynctest.cpp
        TEST_NAME imapsynctest
        LINK_LIBRARIES fjordpostcore Qt6::Test KPim6::kimaptest6
    )
else()
    message(STATUS "KPim6::kimaptest6 nicht gefunden: imapsynctest wird nicht gebaut")
endif()

# Gegen echten Server (GreenMail); wird ohne FJORDPOST_TEST_SERVER übersprungen.
ecm_add_test(servertest.cpp
    TEST_NAME servertest
    LINK_LIBRARIES fjordpostcore Qt6::Test
)
