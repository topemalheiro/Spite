# Sourced by Spite's launchers (bin/*): prints the folders, under lib/fallback/, of the bundled
# libraries this system doesn't have, each after a ':'. A library the system has always comes from
# the system: its copy is newer, and its other libraries may depend on it.
#   spite_fallback_path <install dir>
spite_fallback_path() {
	_spite_cache=$( { /sbin/ldconfig -p || /usr/sbin/ldconfig -p || ldconfig -p; } 2>/dev/null )
	_spite_path=
	_spite_tab=$(printf '\t')
	for _spite_dir in "$1"/lib/fallback/*/; do
		[ -d "$_spite_dir" ] || continue
		_spite_so=$(basename "$_spite_dir")
		case "$_spite_cache" in
		*"$_spite_tab$_spite_so (libc6,x86-64"*) ;;
		*) _spite_path="$_spite_path:${_spite_dir%/}" ;;
		esac
	done
	printf '%s' "$_spite_path"
}
