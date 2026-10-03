#!/bin/bash
mkdir -p dist/lib
mkdir -p dist/platforms
mkdir -p dist/sqldrivers

cp build/TexasSolverGui dist/
cp -r scripts dist/

# Copy all Qt dependencies
ldd build/TexasSolverGui | grep /home/shire/Qt | awk '{print $3}' | xargs -I '{}' cp '{}' dist/lib/

# Copy required platform plugin (XCB is standard for X11/Ubuntu)
cp /home/shire/Qt/6.11.1/gcc_64/plugins/platforms/libqxcb.so dist/platforms/

# Copy SQL drivers
cp /home/shire/Qt/6.11.1/gcc_64/plugins/sqldrivers/libqsqlite.so dist/sqldrivers/

# Copy dependencies for the plugins too!
ldd dist/platforms/libqxcb.so | grep /home/shire/Qt | awk '{print $3}' | xargs -I '{}' cp -n '{}' dist/lib/ 2>/dev/null || true
ldd dist/sqldrivers/libqsqlite.so | grep /home/shire/Qt | awk '{print $3}' | xargs -I '{}' cp -n '{}' dist/lib/ 2>/dev/null || true

# Create the wrapper script
cat << 'WRAPPER' > dist/TexasSolver.sh
#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
export LD_LIBRARY_PATH="$DIR/lib:$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$DIR"
exec "$DIR/TexasSolverGui" "$@"
WRAPPER

chmod +x dist/TexasSolver.sh

echo "Done bundling into dist/"
