# Handwriting search (qt/docs/features/handwriting-search.md): what the recognised words are and how the search matches
# them live in xqt-session (InkText.h); the layout of ink into lines and words, the recognisers and the background
# service are added here as they come.

add_library(xqt-hwr STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/InkLayout.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/InkLayout.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/Recognizer.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/Recognizer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/FakeRecognizer.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/FakeRecognizer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/ModelInfo.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/ModelInfo.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/LanguagePlan.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/LineDataset.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/LineDataset.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/FormManifest.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/FormManifest.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/FormDataset.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/FormDataset.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/FormBench.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/FormBench.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/LanguagePlan.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/MultiRecognizer.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/MultiRecognizer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/LineImage.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/LineImage.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/WordAlignment.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/WordAlignment.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/InkRecognitionService.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/InkRecognitionService.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/InkTextIndexer.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/InkTextIndexer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/InkCopy.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/InkCopy.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/HandwritingSearch.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/HandwritingSearch.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/BeamSearch.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/BeamSearch.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/CtcDecode.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/CtcDecode.cpp)
target_include_directories(xqt-hwr PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src" "${CMAKE_CURRENT_LIST_DIR}/../src/hwr")
target_link_libraries(xqt-hwr PUBLIC xqt-session Qt6::Gui)  # (QImage: the line dataset writes PNGs)
set_target_properties(xqt-hwr PROPERTIES AUTOMOC ON)

# The handwriting models that come with the app (qt/resources/hwr/<name>/: model.json, its files, LICENCE.md): copied
# into the resource dir as hwr-models/<name>/, where HandwritingSearch finds them by their manifests (the build tree's
# share/xournal-qt for development; installed with it by XqtPackage.cmake; on Android in the APK's /xqt-share
# resources, copied to the app's data folder at start by AndroidSetup.cpp). Nothing here names a model: a folder with
# a model.json is one.
file(GLOB _xqt_hwr_models LIST_DIRECTORIES true "${CMAKE_CURRENT_LIST_DIR}/../resources/hwr/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../resources/hwr")
foreach(_xqt_model_dir ${_xqt_hwr_models})
    if(IS_DIRECTORY "${_xqt_model_dir}" AND EXISTS "${_xqt_model_dir}/model.json")
        get_filename_component(_xqt_model "${_xqt_model_dir}" NAME)
        file(GLOB _xqt_model_files LIST_DIRECTORIES false "${_xqt_model_dir}/*")
        foreach(_xqt_file ${_xqt_model_files})
            get_filename_component(_xqt_file_name "${_xqt_file}" NAME)
            configure_file("${_xqt_file}" "${XQT_BUILD_RESOURCE_DIR}/hwr-models/${_xqt_model}/${_xqt_file_name}" COPYONLY)
        endforeach()
    endif()
endforeach()

# The CLI's "hwr-lines", "hwr-form" and "hwr-bench": a document's handwriting as a line dataset (LineDataset.h), a
# filled handwriting form as a dataset (FormDataset.h) and as a benchmark (FormBench.h); Qt only for these commands
if(TARGET xournal-qt-cli)
    target_link_libraries(xournal-qt-cli PRIVATE xqt-hwr)
    target_compile_definitions(xournal-qt-cli PRIVATE XQT_CLI_HWR)
endif()

# The recognisers in ONNX Runtime (TrOCR, CTC). Only the C API's headers are vendored (qt/3rdparty/onnxruntime): the runtime is
# opened when the handwriting search is switched on (dlopen), so the app builds and runs without it.
option(XQT_HWR_ONNX "Handwriting search: the TrOCR recogniser in ONNX Runtime (loaded at run time)" ON)
if(XQT_HWR_ONNX)
    target_sources(xqt-hwr PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/OrtRuntime.h
        ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/OrtRuntime.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/TrocrRecognizer.h
        ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/TrocrRecognizer.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/CtcRecognizer.h
        ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/CtcRecognizer.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/HwrInfo.h
        ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/HwrInfo.cpp)
    target_include_directories(xqt-hwr PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../3rdparty/onnxruntime/include")
    target_compile_definitions(xqt-hwr PUBLIC XQT_HWR_ONNX)
    target_link_libraries(xqt-hwr PRIVATE ${CMAKE_DL_LIBS})
endif()

if(XQT_BUILD_TESTS)
    add_executable(xqt-hwr-tests
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/InkTextTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/InkSearchTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/InkLayoutTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/InkRotationTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/RecognizerTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/InkIndexerTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/InkCopyTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/InkLibraryTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/InkTextLayerTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/ModelDownloadTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/MultiModelTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/LineDatasetTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/FormTest.cpp)
    if(XQT_HWR_ONNX)
        target_sources(xqt-hwr-tests PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/TrocrTest.cpp
            ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/CtcTest.cpp
            ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/BundledModelTest.cpp
            ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/RotationBenchmarkTest.cpp)
    endif()
    target_link_libraries(xqt-hwr-tests PRIVATE xqt-hwr xqt-shell xqt-test-support Qt6::Test GTest::gtest)
    target_include_directories(xqt-hwr-tests PRIVATE "${TEST_CONFIG_DIR}")
    target_compile_definitions(xqt-hwr-tests PRIVATE XQT_BUILD_RESOURCE_DIR="${XQT_BUILD_RESOURCE_DIR}"
        XQT_HWR_TEST_DATA="${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/data")
    gtest_discover_tests(xqt-hwr-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS hwr
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
