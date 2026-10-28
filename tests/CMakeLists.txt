include(ECMAddTests)

ecm_add_tests(
    databasetest.cpp
    messagelistmodeltest.cpp
    LINK_LIBRARIES mopmailcore Qt6::Test
)
