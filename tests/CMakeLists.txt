include(ECMAddTests)

ecm_add_tests(
    databasetest.cpp
    messagelistmodeltest.cpp
    mimetest.cpp
    autoconfigtest.cpp
    LINK_LIBRARIES mopmailcore Qt6::Test
)

ecm_add_test(imapsynctest.cpp
    TEST_NAME imapsynctest
    LINK_LIBRARIES mopmailcore Qt6::Test KPim6::kimaptest6
)
