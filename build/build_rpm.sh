#!/bin/bash
jers_path="/jers"

cd /tmp
rm -rvf ./jers
git clone $jers_path 

# Work out the versioning
MAJOR=$(grep "JERS_MAJOR" /tmp/jers/src/jers.h | awk '{print $3}')
MINOR=$(grep "JERS_MINOR" /tmp/jers/src/jers.h | awk '{print $3}')
PATCH=$(grep "JERS_PATCH" /tmp/jers/src/jers.h | awk '{print $3}')

version="$MAJOR.$MINOR.$PATCH"

mv /tmp/jers /tmp/jers-${version}
cp /tmp/jers-${version}/build/jers.spec /home/rpmbuild/SPECS/

rm -f /home/rpmbuild/SOURCES/*.tar.gz
tar -C /tmp -czf /home/rpmbuild/SOURCES/jers-${version}-${RELEASE}.tar.gz jers-${version}

rpmbuild -ba ~/SPECS/jers.spec \
	--define "rel $RELEASE" 	\
	--define "major_ver $MAJOR" 	\
	--define "minor_ver $MINOR" 	\
	--define "patch $PATCH"

if [[ $? -ne 0 ]] ; then echo "RPMBUILD failed" ; exit 1; fi

# Copy all the artifacts to the directory passed through to the container
find /home/rpmbuild/RPMS /home/rpmbuild/SRPMS -name "jers*-$version-$RELEASE.*rpm" -exec cp -v "{}" /jers/build/out/ \;

echo "Done."
