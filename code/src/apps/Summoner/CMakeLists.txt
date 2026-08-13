# App definition
create_kastle2_app(
    APP_NAME "summoner"
    APP_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/AppSummoner.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common/dsp/effects/ShimmerReverb.cpp
)
