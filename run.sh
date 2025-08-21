#!/usr/bin/sh

platform='linux64'
config='release'
target='screenshaver'

display_help()
{
	echo 'comet.sh [-c|p|h]'
	exit
}

while getopts 'c:p:h' flag; do
	case "${flag}" in
		h) display_help ;;
		c) config="${OPTARG}" ;;
		p) platform="${OPTARG}" ;;
		\?) exit ;;
	esac
done

binpath='./bin/${target}/bin/${platform}/${config}/${target}'

[[ -f "$binpath" ]] && ./$binpath || ./build.sh -c $config -p $platform -r
