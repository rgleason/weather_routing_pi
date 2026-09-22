function(add_cloudsmith_upload_target TARGET_NAME
        BUILD_DIR
        REPO
        VERSION
        TARBALL
        XML
        PKG
        PKG_EXT)

    add_custom_target(${TARGET_NAME}
        COMMAND ${CMAKE_COMMAND} -E echo "Uploading to Cloudsmith: ${REPO}, version ${VERSION}"
        COMMAND cloudsmith push files ${REPO} ${XML}
                --republish --no-wait-for-sync
                --name @PACKAGE_NAME@-metadata
                --version ${VERSION}
                --summary "@PACKAGE@ opencpn plugin metadata"
        COMMAND cloudsmith push files ${REPO} ${TARBALL}
                --republish --no-wait-for-sync
                --name @PACKAGE_NAME@-tarball
                --version ${VERSION}
                --summary "@PACKAGE@ opencpn plugin tarball"
        COMMAND ${CMAKE_COMMAND} -E if ${PKG_EXT} STREQUAL "gz"
                ${CMAKE_COMMAND} -E echo "No installer package to upload"
            ELSE
                cloudsmith push files ${REPO} ${PKG}
                    --republish --no-wait-for-sync
                    --name opencpn-package-@PACKAGE@-${VERSION}.${PKG_EXT}
                    --version ${VERSION}
                    --summary "@PACKAGE@ installer package"
            ENDIF
        WORKING_DIRECTORY ${BUILD_DIR}
        VERBATIM
    )
endfunction()
