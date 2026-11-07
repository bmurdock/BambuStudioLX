# GUI/DeviceWeb
# usage -- GUI about device web for BambuStudio
# date -- 2025.01.01
# status -- Building

# ==================== Node.js/pnpm ???????? deviceweb ????===================

function(prepare_node_env)
    # 定义版本号
    set(NODE_VERSION "22.23.4")
    set(PNPM_VERSION "10.34.9")

    # ??????????????????clean build ??????
    set(NODE_CACHE_DIR "${CMAKE_SOURCE_DIR}/../node-cache")

    # ???????? URL??????hash ??
    if(WIN32)
        set(NODE_URL "https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}-win-x64.zip")
        set(NODE_ARCHIVE_NAME "node-v${NODE_VERSION}-win-x64.zip")
        set(NODE_BIN_DIR "node-v${NODE_VERSION}-win-x64")
        set(NODE_EXEC "node.exe")
        set(NODE_HASH "cbc74b5e0982c9dbb41525fd8fb5dcd32fbee237863f7829072f4639a1ce5f20")
        set(PNPM_URL "https://github.com/pnpm/pnpm/releases/download/v${PNPM_VERSION}/pnpm-win-x64.exe")
        set(PNPM_EXEC_NAME "pnpm.exe")
        set(PNPM_HASH "e0af8d34dd5319f7e9a6c9838779e7e85caf9cfdedf99a7a4106aa1fdcf16d5c")
    elseif(APPLE)
        if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
            set(NODE_URL "https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}-darwin-arm64.tar.gz")
            set(NODE_ARCHIVE_NAME "node-v${NODE_VERSION}-darwin-arm64.tar.gz")
            set(NODE_BIN_DIR "node-v${NODE_VERSION}-darwin-arm64/bin")
            set(NODE_HASH "c9e1dbc748b33ed2d14f42445191ecaf14c56259c7a9a26d0042c49ec2b66428")

            set(PNPM_URL "https://github.com/pnpm/pnpm/releases/download/v${PNPM_VERSION}/pnpm-macos-arm64")
            set(PNPM_HASH "984b2f4316de371dc314243649e5f9ea392195d541dedbfdb990dc24d6c6e1a2")
        else()
            set(NODE_URL "https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}-darwin-x64.tar.gz")
            set(NODE_ARCHIVE_NAME "node-v${NODE_VERSION}-darwin-x64.tar.gz")
            set(NODE_BIN_DIR "node-v${NODE_VERSION}-darwin-x64/bin")
            set(NODE_HASH "efcc2b0db6f94bf53eddb038fe2b370277a042bd29e4f19b8f8e23598795b819")

            set(PNPM_URL "https://github.com/pnpm/pnpm/releases/download/v${PNPM_VERSION}/pnpm-macos-x64")
            set(PNPM_HASH "58b35a997d2d5167ef60e24b6ac0d71b76a5e1a4b3346cb1075b1dface50bd32")
        endif()
        set(NODE_EXEC "node")
        set(PNPM_EXEC_NAME "pnpm")
    else()
        if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
            set(NODE_URL "https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}-linux-arm64.tar.xz")
            set(NODE_ARCHIVE_NAME "node-v${NODE_VERSION}-linux-arm64.tar.xz")
            set(NODE_BIN_DIR "node-v${NODE_VERSION}-linux-arm64/bin")
            set(NODE_EXEC "node")
            set(NODE_HASH "6287aa46b6c375d816e63ae66d024fbbfd70404825aabdfdf898a970871d2f4b")

            set(PNPM_URL "https://github.com/pnpm/pnpm/releases/download/v${PNPM_VERSION}/pnpm-linux-arm64")
            set(PNPM_EXEC_NAME "pnpm")
            set(PNPM_HASH "85abd31b96964e6510cecb3b366ba024287191e0ee5978e5a9e2d3beb4a377d1")
        else()
            set(NODE_URL "https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}-linux-x64.tar.xz")
            set(NODE_ARCHIVE_NAME "node-v${NODE_VERSION}-linux-x64.tar.xz")
            set(NODE_BIN_DIR "node-v${NODE_VERSION}-linux-x64/bin")
            set(NODE_EXEC "node")
            set(NODE_HASH "1805bcd35e6815559bd2eb97db438d3fdb0eb3a5e6a977b7c1badca76337e9ce")

            set(PNPM_URL "https://github.com/pnpm/pnpm/releases/download/v${PNPM_VERSION}/pnpm-linux-x64")
            set(PNPM_EXEC_NAME "pnpm")
            set(PNPM_HASH "3ec79240658f0ee4e8977f21ee2c598c7a8b8d06b86dc564f2ec6cb099fd8ac2")
        endif()
    endif()

    # ?????????????
    set(NODE_EXECUTABLE "${NODE_CACHE_DIR}/${NODE_BIN_DIR}/${NODE_EXEC}")
    # pnpm ?? node ???????? pnpm ??PATH ?????? node
    set(PNPM_EXECUTABLE "${NODE_CACHE_DIR}/${NODE_BIN_DIR}/${PNPM_EXEC_NAME}")

    message(STATUS "Checking for node binary at ${NODE_EXECUTABLE}")
    message(STATUS "Checking for pnpm binary ${PNPM_EXECUTABLE}")

    # ============ Node.js 下载 + 解压 ============
    if(NOT EXISTS "${NODE_EXECUTABLE}")
        message(STATUS "Downloading Node.js v${NODE_VERSION} from ${NODE_URL}")
        file(MAKE_DIRECTORY "${NODE_CACHE_DIR}")

        file(DOWNLOAD
            ${NODE_URL}
            "${NODE_CACHE_DIR}/${NODE_ARCHIVE_NAME}"
            SHOW_PROGRESS
            STATUS node_dl_status
            EXPECTED_HASH SHA256=${NODE_HASH}
            TLS_VERIFY ON
        )
        list(GET node_dl_status 0 node_dl_code)
        list(GET node_dl_status 1 node_dl_msg)
        if(NOT node_dl_code EQUAL 0)
            file(REMOVE "${NODE_CACHE_DIR}/${NODE_ARCHIVE_NAME}")
            message(FATAL_ERROR "Failed to download Node.js: ${node_dl_msg}")
        endif()

        message(STATUS "Extracting Node.js...")
        if(WIN32)
            execute_process(
                COMMAND powershell -NoProfile -Command
                        "Expand-Archive -Path '${NODE_CACHE_DIR}/${NODE_ARCHIVE_NAME}' -DestinationPath '${NODE_CACHE_DIR}' -Force"
                WORKING_DIRECTORY "${NODE_CACHE_DIR}"
                RESULT_VARIABLE extract_result
                OUTPUT_VARIABLE extract_output
                ERROR_VARIABLE extract_error
            )
        else()
            execute_process(
                COMMAND tar -xf "${NODE_ARCHIVE_NAME}"
                WORKING_DIRECTORY "${NODE_CACHE_DIR}"
                RESULT_VARIABLE extract_result
                OUTPUT_VARIABLE extract_output
                ERROR_VARIABLE extract_error
            )
        endif()

        if(NOT extract_result EQUAL 0)
            message(FATAL_ERROR "Failed to extract Node.js archive: ${extract_error}")
        endif()

        # ????????????
        file(REMOVE "${NODE_CACHE_DIR}/${NODE_ARCHIVE_NAME}")

        if(NOT EXISTS "${NODE_EXECUTABLE}")
            message(FATAL_ERROR "Node.js extraction finished but executable not found: ${NODE_EXECUTABLE}")
        endif()

        message(STATUS "Node.js extracted to: ${NODE_EXECUTABLE}")
    else()
        message(STATUS "Found cached Node.js: ${NODE_EXECUTABLE}")
    endif()

    # ============ pnpm ????????============
    if(NOT EXISTS "${PNPM_EXECUTABLE}")
        message(STATUS "Downloading pnpm v${PNPM_VERSION} from ${PNPM_URL}")

        file(DOWNLOAD
            ${PNPM_URL}
            "${PNPM_EXECUTABLE}"
            SHOW_PROGRESS
            STATUS pnpm_dl_status
            EXPECTED_HASH SHA256=${PNPM_HASH}
            TLS_VERIFY ON
        )
        list(GET pnpm_dl_status 0 pnpm_dl_code)
        list(GET pnpm_dl_status 1 pnpm_dl_msg)
        if(NOT pnpm_dl_code EQUAL 0)
            file(REMOVE "${PNPM_EXECUTABLE}")
            message(FATAL_ERROR "Failed to download pnpm: ${pnpm_dl_msg}")
        endif()

        if(NOT WIN32)
            execute_process(
                COMMAND chmod +x "${PNPM_EXECUTABLE}"
                RESULT_VARIABLE chmod_result
                ERROR_VARIABLE chmod_error
            )
            if(NOT chmod_result EQUAL 0)
                message(FATAL_ERROR "Failed to chmod +x pnpm: ${chmod_error}")
            endif()
        endif()

        message(STATUS "pnpm installed: ${PNPM_EXECUTABLE}")
    else()
        message(STATUS "Found cached pnpm: ${PNPM_EXECUTABLE}")
    endif()

    # ??????????????
    set(NODE_EXECUTABLE "${NODE_EXECUTABLE}" PARENT_SCOPE)
    set(PNPM_EXECUTABLE "${PNPM_EXECUTABLE}" PARENT_SCOPE)
endfunction()

# ???????? config ??????
prepare_node_env()

# ==================== ?????? ====================

set(DEVICE_PAGE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/device_page")
set(DEVICE_PAGE_DIST_DIR "${DEVICE_PAGE_SRC_DIR}/dist")
set(DEVICE_PAGE_TARGET_DIR "${CMAKE_SOURCE_DIR}/resources/web/device_page/dist")

get_filename_component(NODE_BIN_PATH "${NODE_EXECUTABLE}" DIRECTORY)

if(WIN32)
    # ??PATH???\???????????????
    set(DEVICE_PAGE_PATH_UNESCAPED "${NODE_BIN_PATH};$ENV{PATH}")
    string(REPLACE "\;" ";" DEVICE_PAGE_PATH "${DEVICE_PAGE_PATH_UNESCAPED}")
else()
    # Unix PATH ???? :
    set(DEVICE_PAGE_PATH "${NODE_BIN_PATH}:$ENV{PATH}")
endif()

set(PNPM_STORE_DIR "${CMAKE_CURRENT_BINARY_DIR}/.pnpm-store")
file(MAKE_DIRECTORY "${PNPM_STORE_DIR}")

set(DEVICE_PAGE_BUILD_STAMP "${CMAKE_CURRENT_BINARY_DIR}/device_page.stamp")

file(GLOB_RECURSE DEVICE_PAGE_SRC_FILES CONFIGURE_DEPENDS
    "${DEVICE_PAGE_SRC_DIR}/src/*"
)
file(GLOB_RECURSE DEVICE_PAGE_LOCALE_FILES CONFIGURE_DEPENDS
    "${DEVICE_PAGE_SRC_DIR}/locales/*"
)
file(GLOB_RECURSE DEVICE_PAGE_PUBLIC_FILES CONFIGURE_DEPENDS
    "${DEVICE_PAGE_SRC_DIR}/public/*"
)

set(DEVICE_PAGE_BUILD_INPUTS
    ${DEVICE_PAGE_SRC_FILES}
    ${DEVICE_PAGE_LOCALE_FILES}
    ${DEVICE_PAGE_PUBLIC_FILES}
    "${DEVICE_PAGE_SRC_DIR}/index.html"
    "${DEVICE_PAGE_SRC_DIR}/package.json"
    "${DEVICE_PAGE_SRC_DIR}/pnpm-lock.yaml"
    "${DEVICE_PAGE_SRC_DIR}/vite.config.ts"
    "${DEVICE_PAGE_SRC_DIR}/legacy-webkit.postcss.ts"
    "${DEVICE_PAGE_SRC_DIR}/tools/check-legacy-css.mjs"
    "${DEVICE_PAGE_SRC_DIR}/tsconfig.json"
    "${DEVICE_PAGE_SRC_DIR}/tsconfig.app.json"
    "${DEVICE_PAGE_SRC_DIR}/tsconfig.node.json"
    "${DEVICE_PAGE_SRC_DIR}/eslint.config.js"
    "${DEVICE_PAGE_SRC_DIR}/i18next-parser.config.js"
)

add_custom_command(
    OUTPUT "${DEVICE_PAGE_BUILD_STAMP}"
    COMMAND echo "Running PNPM install"
    COMMAND ${CMAKE_COMMAND} -E env "PATH=${DEVICE_PAGE_PATH}" "CI=true" ${PNPM_EXECUTABLE} install --frozen-lockfile --store-dir ${PNPM_STORE_DIR}
    COMMAND echo "Running PNPM run build"
    COMMAND ${CMAKE_COMMAND} -E env "PATH=${DEVICE_PAGE_PATH}" ${PNPM_EXECUTABLE} run build
    COMMAND echo "Removing page target directory"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${DEVICE_PAGE_TARGET_DIR}"
    COMMAND echo "Creating page target directory"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEVICE_PAGE_TARGET_DIR}"
    COMMAND echo "Copying directory to target"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${DEVICE_PAGE_DIST_DIR}" "${DEVICE_PAGE_TARGET_DIR}"
    COMMAND echo "Create device page stamp file"
    COMMAND ${CMAKE_COMMAND} -E touch "${DEVICE_PAGE_BUILD_STAMP}"
    DEPENDS ${DEVICE_PAGE_BUILD_INPUTS}
    WORKING_DIRECTORY ${DEVICE_PAGE_SRC_DIR}
    COMMENT "Building device_page web bundle when sources change..."
    VERBATIM
)

# Not ALL: rebuilt only when a dependent target (libslic3r_gui / BambuStudio) is built.
if (CMAKE_GENERATOR MATCHES "Visual Studio")
    # Visual Studio keeps prompting that this utility project is out of date unless
    # the generated per-target marker also exists alongside the real stamp file.
    set(DEVICE_PAGE_BUILD_TARGET_MARKER "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/device_page_build")
    add_custom_target(device_page_build
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles"
        COMMAND ${CMAKE_COMMAND} -E touch "${DEVICE_PAGE_BUILD_TARGET_MARKER}"
        DEPENDS "${DEVICE_PAGE_BUILD_STAMP}"
        VERBATIM
    )
else()
    add_custom_target(device_page_build DEPENDS "${DEVICE_PAGE_BUILD_STAMP}")
endif()

# ==================== DeviceWebHtml: native HTML front-end ====================
# Plain static assets - no Node/pnpm/Vite involved, so this deliberately bypasses
# prepare_node_env() entirely and is just a directory copy.

set(DEVICE_PAGE_HTML_SRC_DIR    "${CMAKE_CURRENT_SOURCE_DIR}/DeviceWebHtml/web")
set(DEVICE_PAGE_HTML_TARGET_DIR "${CMAKE_SOURCE_DIR}/resources/web/device_page_html")
set(DEVICE_PAGE_HTML_STAMP      "${CMAKE_CURRENT_BINARY_DIR}/device_page_html.stamp")

file(GLOB_RECURSE DEVICE_PAGE_HTML_FILES CONFIGURE_DEPENDS
    "${DEVICE_PAGE_HTML_SRC_DIR}/*"
)

add_custom_command(
    OUTPUT "${DEVICE_PAGE_HTML_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${DEVICE_PAGE_HTML_TARGET_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEVICE_PAGE_HTML_TARGET_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${DEVICE_PAGE_HTML_SRC_DIR}" "${DEVICE_PAGE_HTML_TARGET_DIR}"
    COMMAND ${CMAKE_COMMAND} -E touch "${DEVICE_PAGE_HTML_STAMP}"
    DEPENDS ${DEVICE_PAGE_HTML_FILES}
    COMMENT "Copying device_page_html static assets..."
    VERBATIM
)

if (CMAKE_GENERATOR MATCHES "Visual Studio")
    # Match the workaround above so VS can satisfy its utility-project up-to-date check.
    set(DEVICE_PAGE_HTML_TARGET_MARKER "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/device_page_html_build")
    add_custom_target(device_page_html_build
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles"
        COMMAND ${CMAKE_COMMAND} -E touch "${DEVICE_PAGE_HTML_TARGET_MARKER}"
        DEPENDS "${DEVICE_PAGE_HTML_STAMP}"
        VERBATIM
    )
else()
    add_custom_target(device_page_html_build DEPENDS "${DEVICE_PAGE_HTML_STAMP}")
endif()

list(APPEND SLIC3R_GUI_SOURCES
    GUI/DeviceWeb/DeviceHttpServer.cpp
    GUI/DeviceWeb/DeviceHttpServer.hpp
    GUI/DeviceWeb/DeviceWebPage.cpp
    GUI/DeviceWeb/DeviceWebPage.hpp
    GUI/DeviceWeb/DeviceWebHost.cpp
    GUI/DeviceWeb/DeviceWebHost.hpp
    GUI/DeviceWeb/DeviceWebBridge.cpp
    GUI/DeviceWeb/DeviceWebBridge.hpp
    GUI/DeviceWeb/DeviceWebModel.hpp
    GUI/DeviceWeb/IViewModel.hpp
    GUI/DeviceWeb/DeviceWebManager.cpp
    GUI/DeviceWeb/DeviceWebManager.hpp
    GUI/DeviceWeb/ViewModels/FilamentManager/FilamentManagerVM.cpp
    GUI/DeviceWeb/ViewModels/FilamentManager/FilamentManagerVM.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/DevicePageDialogHelpers.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/DevicePageDialogHelpers.h
    GUI/DeviceWeb/ViewModels/DevicePage/AmsControlWeb/ViewModel.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsControlWeb/ViewModel.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsControlWeb/ViewModelActions.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsControlWeb/ViewModelActions.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsControlWeb/ViewModelDataBuilder.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsControlWeb/ViewModelDataBuilder.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsControlWeb/ViewModelDisplayBuilder.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsControlWeb/ViewModelDisplayBuilder.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsControlWeb/ViewModelLayoutBuilder.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsControlWeb/ViewModelLayoutBuilder.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsControlWeb/Schema.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsControlWeb/SchemaData.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsControlWeb/SchemaDisplay.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ViewModel.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/PageFilament.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/PageHotend.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ViewModel.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ActionHandlerExtAms.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ActionHandlerExtAms.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ExtFilamentEditDialog.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ExtFilamentEditDialog.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/BridgeDebugDialog.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ActionHandlerFilament.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/BuilderHotendRackProgressStatus.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/BuilderHotendRackProgressStatus.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/BuilderFilamentChangeStatus.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/BuilderFilamentChangeStatus.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/BuilderFilamentManualPullStatus.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/BuilderFilamentManualPullStatus.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/FilamentManualPullDialog.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/FilamentManualPullDialog.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ResolverSlotState.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ResolverSlotState.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ResolverDeviceState.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ResolverDeviceState.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ActionHandlerHotend.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/SchemaExtOperationPage.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/SchemaFilamentSlot.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/SchemaFilamentPage.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/SchemaFilamentChangeStatus.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/SchemaFilamentManualPull.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/SchemaHotendRackProgress.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/SchemaHotendSlot.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/SchemaHotendPage.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/Schema.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/Modes/IFilamentPageModeBuilder.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/Modes/BatchModeCommon.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/Modes/BatchModeCommon.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/Modes/NormalModeBuilder.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/Modes/NormalModeBuilder.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/Modes/BatchAmsLoadModeBuilder.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/Modes/BatchAmsLoadModeBuilder.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/Modes/BatchExtLoadModeBuilder.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/Modes/BatchExtLoadModeBuilder.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/Modes/BatchUnloadModeBuilder.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/Modes/BatchUnloadModeBuilder.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsMappingHotend/DevicePageAmsMappingHotendVM.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AmsMappingHotend/DevicePageAmsMappingHotendVM.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AutoRefill_RefillInfo/ViewModel.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AutoRefill_RefillInfo/ViewModel.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AutoRefill_RefillInfo/refill_info/Schema.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AutoRefill_RefillInfo/refill_info/Builder.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AutoRefill_RefillInfo/refill_info/Builder.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AutoRefill_RefillInfo/refill_info/Actions.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AutoRefill_RefillInfo/refill_info/Actions.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AutoRefill_RefillInfo/add_refill_filaments/Schema.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AutoRefill_RefillInfo/add_refill_filaments/Builder.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AutoRefill_RefillInfo/add_refill_filaments/Builder.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AutoRefill_RefillInfo/add_refill_filaments/Actions.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AutoRefill_RefillInfo/add_refill_filaments/Actions.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/AutoRefill_RefillInfo/add_refill_filaments/BlockCodeUtil.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/AutoRefill_RefillInfo/add_refill_filaments/BlockCodeUtil.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/ConnectedSlots/ViewModel.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/ConnectedSlots/ViewModel.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/ConnectedSlots/ViewModelState.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/ConnectedSlots/ViewModelActions.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/ConnectedSlots/ViewModelActions.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/ConnectedSlots/PageConnectedSlots.cpp
    GUI/DeviceWeb/ViewModels/DevicePage/ConnectedSlots/PageConnectedSlots.hpp
    GUI/DeviceWeb/ViewModels/DevicePage/ConnectedSlots/Schema.hpp
    GUI/DeviceWeb/ViewModels/PrintQueue/PrintQueueVM.cpp
    GUI/DeviceWeb/ViewModels/PrintQueue/PrintQueueVM.hpp
)
set(SLIC3R_GUI_SOURCES ${SLIC3R_GUI_SOURCES} PARENT_SCOPE)
