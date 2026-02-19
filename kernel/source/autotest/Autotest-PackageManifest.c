/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.


    Package manifest parser autotests

\************************************************************************/

#include "Autotest.h"

#include "CoreString.h"
#include "Log.h"
#include "package/PackageManifest.h"

/***************************************************************************/

/**
 * @brief Assert one boolean condition in package manifest tests.
 * @param Condition Assertion condition.
 * @param Results Test aggregate result.
 * @param Message Failure message.
 */
static void PackageManifestAssert(BOOL Condition, TEST_RESULTS* Results, LPCSTR Message) {
    if (Results == NULL) return;

    Results->TestsRun++;
    if (Condition) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("[TestPackageManifest] Assertion failed: %s"), Message);
    }
}

/***************************************************************************/

/**
 * @brief Test nominal parse with top-level keys.
 * @param Results Test aggregate result.
 */
static void TestPackageManifestTopLevel(TEST_RESULTS* Results) {
    static const STR ManifestText[] =
        "name = \"shell\"\n"
        "version = \"1.2.3\"\n"
        "provides = [\"api.shell\", \"api.console\"]\n"
        "requires = [\"api.core\"]\n";
    PACKAGE_MANIFEST Manifest;
    U32 Status = PackageManifestParseText((LPCSTR)ManifestText, &Manifest);

    PackageManifestAssert(Status == PACKAGE_MANIFEST_STATUS_OK, Results, TEXT("top-level parse status"));
    if (Status == PACKAGE_MANIFEST_STATUS_OK) {
        PackageManifestAssert(StringCompare(Manifest.Name, TEXT("shell")) == 0, Results, TEXT("name"));
        PackageManifestAssert(StringCompare(Manifest.Version, TEXT("1.2.3")) == 0, Results, TEXT("version"));
        PackageManifestAssert(Manifest.ProvidesCount == 2, Results, TEXT("provides count"));
        PackageManifestAssert(Manifest.RequiresCount == 1, Results, TEXT("requires count"));
        PackageManifestAssert(StringCompare(Manifest.Provides[0], TEXT("api.shell")) == 0,
            Results,
            TEXT("provides[0]"));
        PackageManifestAssert(StringCompare(Manifest.Requires[0], TEXT("api.core")) == 0,
            Results,
            TEXT("requires[0]"));
    }

    PackageManifestRelease(&Manifest);
}

/***************************************************************************/

/**
 * @brief Test parse with [package] section keys.
 * @param Results Test aggregate result.
 */
static void TestPackageManifestPackageSection(TEST_RESULTS* Results) {
    static const STR ManifestText[] =
        "[package]\n"
        "name = \"netget\"\n"
        "version = \"0.9.0\"\n"
        "provides = [\"api.net.get\"]\n"
        "requires = [\"api.net.base\", \"api.crypto.sha256\"]\n";
    PACKAGE_MANIFEST Manifest;
    U32 Status = PackageManifestParseText((LPCSTR)ManifestText, &Manifest);

    PackageManifestAssert(Status == PACKAGE_MANIFEST_STATUS_OK, Results, TEXT("section parse status"));
    if (Status == PACKAGE_MANIFEST_STATUS_OK) {
        PackageManifestAssert(StringCompare(Manifest.Name, TEXT("netget")) == 0, Results, TEXT("section name"));
        PackageManifestAssert(Manifest.RequiresCount == 2, Results, TEXT("section requires count"));
    }

    PackageManifestRelease(&Manifest);
}

/***************************************************************************/

/**
 * @brief Test missing required fields.
 * @param Results Test aggregate result.
 */
static void TestPackageManifestMissingFields(TEST_RESULTS* Results) {
    static const STR MissingName[] = "version = \"1.0\"\n";
    static const STR MissingVersion[] = "name = \"pkg\"\n";
    PACKAGE_MANIFEST Manifest;
    U32 Status;

    Status = PackageManifestParseText((LPCSTR)MissingName, &Manifest);
    PackageManifestAssert(Status == PACKAGE_MANIFEST_STATUS_MISSING_NAME, Results, TEXT("missing name status"));
    PackageManifestRelease(&Manifest);

    Status = PackageManifestParseText((LPCSTR)MissingVersion, &Manifest);
    PackageManifestAssert(Status == PACKAGE_MANIFEST_STATUS_MISSING_VERSION,
        Results,
        TEXT("missing version status"));
    PackageManifestRelease(&Manifest);
}

/***************************************************************************/

/**
 * @brief Test invalid list formatting rejection.
 * @param Results Test aggregate result.
 */
static void TestPackageManifestInvalidList(TEST_RESULTS* Results) {
    static const STR BadList[] =
        "name = \"pkg\"\n"
        "version = \"1.0\"\n"
        "requires = [api.core]\n";
    PACKAGE_MANIFEST Manifest;
    U32 Status = PackageManifestParseText((LPCSTR)BadList, &Manifest);

    PackageManifestAssert(Status == PACKAGE_MANIFEST_STATUS_INVALID_LIST, Results, TEXT("invalid list status"));
    PackageManifestRelease(&Manifest);
}

/***************************************************************************/

/**
 * @brief Run package manifest parser test suite.
 * @param Results Test aggregate result.
 */
void TestPackageManifest(TEST_RESULTS* Results) {
    if (Results == NULL) return;

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    TestPackageManifestTopLevel(Results);
    TestPackageManifestPackageSection(Results);
    TestPackageManifestMissingFields(Results);
    TestPackageManifestInvalidList(Results);
}
