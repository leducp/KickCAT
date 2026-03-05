#!/usr/bin/env bash

set -ue

KICKCAT_DIR=$(dirname "$(realpath $0)")/../

if [ "$#" -ne 2 ]; then
  echo "Usage: $0 <version> <conan-io/conan-center-index's fork repository>"
  exit 1
fi

version=$1
repository=$2

if [ ! -d "${repository}" ]; then
  echo "conan-io/conan-center-index's fork repository doesn't exist: ${repository}"
  exit 1
fi

cd "${repository}"
if [ ! -d .git ]; then
  echo "not a Git repository: ${repository}"
  exit 1
fi

if ! git remote | grep -q '^upstream$'; then
  echo "'upstream' remote doesn't exist: ${repository}"
  echo "Run the following command line in ${repository}:"
  echo "  git remote add upstream https://github.com/conan-io/conan-center-index.git"
  exit 1
fi

echo "Updating repository: ${repository}"
git fetch --all --prune --tags --force
git checkout master
git rebase upstream/master

branch="kickcat-${version}"
echo "Creating branch: ${branch}"
git branch -D ${branch} || :
git checkout -b ${branch}

recipes_kickcat=recipes/kickcat
echo "Updating: ${recipes_kickcat}"

# Copy the recipe from KickCAT repo and adapt it for Conan Center Index
cp "${KICKCAT_DIR}/conan/all/conanfile.py" "${recipes_kickcat}/all/conanfile.py"

# Strip local export_sources() method and enable CCI source() method
# We remove the method definition and all indented lines following it
sed -i '/def export_sources(self):/,/    def source(self):/ { /def source(self):/!d }' "${recipes_kickcat}/all/conanfile.py"
sed -i 's/# get(self,/get(self,/' "${recipes_kickcat}/all/conanfile.py"
sed -i 's/pass/ /' "${recipes_kickcat}/all/conanfile.py"

tar_gz_url="https://github.com/leducp/KickCAT/archive/refs/tags/${version}.zip"
tmp=$(mktemp)
curl -L -o "$tmp" "$tar_gz_url"
sha256sum=$(sha256sum "$tmp" | cut -d' ' -f1)
rm "$tmp"

sed \
  -i.bak \
  -e "1a\\
\  \"${version}\":" \
  -e "1a\\
\    folder:\ all" \
  ${recipes_kickcat}/config.yml
rm ${recipes_kickcat}/config.yml.bak
sed \
  -i.bak \
  -e "1a\\
\  \"${version}\":" \
  -e "1a\\
\    url: \"${tar_gz_url}\"" \
  -e "1a\\
\    sha256: \"${sha256sum}\"" \
  ${recipes_kickcat}/all/conandata.yml
rm ${recipes_kickcat}/all/conandata.yml.bak
git add ${recipes_kickcat}/config.yml
git add ${recipes_kickcat}/all/conandata.yml
git add ${recipes_kickcat}/all/conanfile.py
git commit -m "kickcat: add version ${version}"

git push origin ${branch}


owner=$(git remote get-url origin | \
          grep -o '[a-zA-Z0-9_-]*/conan-center-index' | \
          cut -d/ -f1)
echo "Create a pull request:"
echo "  https://github.com/${owner}/conan-center-index/pull/new/${branch}"
