include(ECMAddTests)

ecm_add_tests(
    databasetest.cpp
    messagelistmodeltest.cpp
    mimetest.cpp
    autoconfigtest.cpp
    smtptest.cpp
    LINK_LIBRARIES mopmailcore Qt6::Test
)

ecm_add_test(imapsynctest.cpp
    TEST_NAME imapsynctest
    LINK_LIBRARIES mopmailcore Qt6::Test KPim6::kimaptest6
)

# Gegen echten Server (GreenMail); wird ohne MOPMAIL_TEST_SERVER übersprungen.
ecm_add_test(servertest.cpp
    TEST_NAME servertest
    LINK_LIBRARIES mopmailcore Qt6::Test
)
