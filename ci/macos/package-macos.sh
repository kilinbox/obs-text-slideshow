#!/bin/bash

set -e

script_dir=$(dirname "$0")
source "$script_dir/../ci_includes.generated.sh"

OSTYPE=$(uname)

if [ "${OSTYPE}" != "Darwin" ]; then
    echo "[Error] macOS package script can be run on Darwin-type OS only."
    exit 1
fi

echo "=> Preparing package build"
export QT_CELLAR_PREFIX="$(/usr/bin/find /usr/local/Cellar/qt -d 1 | sort -t '.' -k 1,1n -k 2,2n -k 3,3n | tail -n 1)"

FILENAME_UNSIGNED="$PLUGIN_NAME-$PLUGIN_VERSION-Unsigned.pkg"
FILENAME="$PLUGIN_NAME-$PLUGIN_VERSION.pkg"

echo "=> Modifying $PLUGIN_NAME.so"
install_name_tool \
	-change ~/obsdeps/lib/QtWidgets.framework/Versions/5/QtWidgets \
		@executable_path/../Frameworks/QtWidgets.framework/Versions/5/QtWidgets \
	-change ~/obsdeps/lib/QtGui.framework/Versions/5/QtGui \
		@executable_path/../Frameworks/QtGui.framework/Versions/5/QtGui \
	-change ~/obsdeps/lib/QtCore.framework/Versions/5/QtCore \
		@executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore \
	./build/$PLUGIN_NAME.so

# Check if replacement worked
echo "=> Dependencies for $PLUGIN_NAME"
otool -L ./build/$PLUGIN_NAME.so

if [[ "$RELEASE_MODE" == "True" ]]; then
	echo "=> Signing plugin binary: $PLUGIN_NAME.so"
	codesign --sign "$CODE_SIGNING_IDENTITY" ./build/$PLUGIN_NAME.so
else
	echo "=> Skipped plugin codesigning"
fi

echo "=> Actual package build"
packagesbuild ./installer/installer-macOS.generated.pkgproj

echo "=> Renaming $PLUGIN_NAME.pkg to $FILENAME"
mv ./release/$PLUGIN_NAME.pkg ./release/$FILENAME_UNSIGNED

if [[ "$RELEASE_MODE" == "True" ]]; then
	echo "=> Signing installer: $FILENAME"
	productsign \
		--sign "$INSTALLER_SIGNING_IDENTITY" \
		./release/$FILENAME_UNSIGNED \
		./release/$FILENAME
	rm ./release/$FILENAME_UNSIGNED

	echo "=> Submitting installer $FILENAME for notarization"
	zip -r ./release/$FILENAME.zip ./release/$FILENAME
	UPLOAD_RESULT=$(xcrun altool \
		--notarize-app \
		--primary-bundle-id "$MACOS_BUNDLEID" \
		--username "$AC_USERNAME" \
		--password "$AC_PASSWORD" \
		--asc-provider "$AC_PROVIDER_SHORTNAME" \
		--file "./release/$FILENAME.zip")
	rm ./release/$FILENAME.zip

	REQUEST_UUID=$(echo $UPLOAD_RESULT | awk -F ' = ' '/RequestUUID/ {print $2}')
	echo "Request UUID: $REQUEST_UUID"

	echo "=> Wait for notarization result"
	# Pieces of code borrowed from rednoah/notarized-app
	while sleep 30 && date; do
		CHECK_RESULT=$(xcrun altool \
			--notarization-info "$REQUEST_UUID" \
			--username "$AC_USERNAME" \
			--password "$AC_PASSWORD" \
			--asc-provider "$AC_PROVIDER_SHORTNAME")
		echo $CHECK_RESULT

		if ! grep -q "Status: in progress" <<< "$CHECK_RESULT"; then
			echo "=> Staple ticket to installer: $FILENAME"
			xcrun stapler staple ./release/$FILENAME
			break
		fi
	done
else
	echo "=> Skipped installer codesigning and notarization"
fi
