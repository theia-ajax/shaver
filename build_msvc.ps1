<#
.SYNOPSIS
    Generates (if necessary) a solution file with premake5 using the premake5.lua config file in the same directory.
    Builds the solution for Win32 or Win64 targets with Visual Studio C++ build tools.
.PARAMETER Target
    The name of the solution that will be generated under the bin/ directory and then built.
    It should not include the .sln extension.
.PARAMETER Toolset
    Which Visual Studio toolset by product line version number (e.g. 2019) or "latest" which corresponds to the most recent version installed.
    Default="latest"
.PARAMETER BuildAction
    Corresponds to msbuild build action; "Build", "Clean", or "Rebuild".
    Default="Build"
.PARAMETER Platform
    Name of the configuration to use. Available platforms defined in premake5.lua.
    Default="Win64"
.PARAMETER Configuration
    Which build configuration to use. Available configurations defined in premake5.lua.
    Default="Debug"
.EXAMPLE
    C:\PS> .\build.ps1 -Target cultist
    Runs the Build target on the solution located at bin/cultist.sln with defaults.

    C:\PS> .\build.ps1 -Target cultist -Toolset 2017 -BuildAction Clean -Platform Win32 -Configuration Release
    Runs the Clean target on the solution located at bin/cultist.sln with the 2017 toolset for Win32 Release.
.NOTES
    Author: tdjx
    Date:   June 28, 2020
#>

param (
    [Parameter(Mandatory = $true, HelpMessage = "Name of the solution to generate and build...")]
    [string] $target,

    [ValidateSet("latest", "2019", "2017", "2015", "2013", "2012", "2010")]
    [string] $toolset = "latest",

    [ValidateSet("Build", "Rebuild", "Clean", "Run", "BuildAndRun")]
    [string] $buildAction = "Build",

    [ValidateSet("Win32", "Win64")]
    [string] $platform = "Win64",

    [ValidateSet("Debug", "Release")]
    [string] $configuration = "Debug",

    [switch] $installDependencies = $false,
    [switch] $skipPremake = $false
)

$dependencies = "sdl2"

$toolset_version_from_product_line = @{
    "2019" = "16"
    "2017" = "15"
    "2015" = "14"
    "2013" = "13"
    "2012" = "12"
    "2010" = "11"
}

$build_action_steps = @{
    "Build"       = [PSCustomObject]@{
        do_build = $true
        do_run   = $false
        do_clean = $false
    }
    "Rebuild"     = [PSCustomObject]@{
        do_build = $true
        do_run   = $false
        do_clean = $false
    }
    "Clean"       = [PSCustomObject]@{
        do_build = $true
        do_run   = $false
        do_clean = $true
    }
    "BuildAndRun" = [PSCustomObject]@{
        do_build = $true
        do_run   = $true
        do_clean = $false
    }
    "Run"         = [PSCustomObject]@{
        do_build = $false
        do_run   = $true
        do_clean = $false
    }
}

$ms_build_actions = @{
    "Build"       = "Build"
    "Rebuild"     = "Rebuild" 
    "Clean"       = "Clean"
    "BuildAndRun" = "Build"
    "Run"         = "FAIL"
}

function Get-InstalledVcpkgList() {
    if ($vcpkg -eq $null) {
        $vcpkg = "vcpkg.exe"
    }

    # Find installed packages
    $vcpkg_list = Invoke-Expression "$vcpkg list"
    $vcpkg_list = $vcpkg_list.Split([System.Environment]::NewLine)
    $vcpkg_installed = [System.Collections.ArrayList]@()
    foreach ($pkg in $vcpkg_list) {
        $installed = -split $pkg
        $installed = $installed[0]
        $vcpkg_installed.Add($installed) > $null
    }
    return $vcpkg_installed
}

function Install-VcpkgDependencies() {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string] $triplet,
        [Parameter(Mandatory)]
        [array] $dependencies,
        [array] $installed
    )
    
    if ($vcpkg -eq $null) {
        $vcpkg = "vcpkg.exe"
    }

    if ($installed -eq $null) {
        $installed = Get-InstalledVcpkgList
    }

    $full_dependencies = [System.Collections.ArrayList]@()
    
    # for dependencies with submodules split the submodules out into separate dependencies
    # e.g. imgui[sdl2-binding,vulkan-binding] becomes:
    #      imgui[sdl2-binding]
    #      imgui[vulkan-binding]
    foreach ($dependency in $dependencies) {
        $chunks = $dependency.split("[")
        if ($chunks.Count -gt 1) {
            $package = $chunks[0]
            $submodules = $chunks[1].split(",")
            foreach ($_ in $submodules) {
                $cleaned = $_.split("]")[0]
                $full_dependencies.Add("$package[$cleaned]") > $null
            }
        }
        else {
            $full_dependencies.Add($dependency) > $null
        }
    }
    
    $vcpkg_installed = Get-InstalledVcpkgList

    # For all dependencies that are not installed, install them
    foreach ($dependency in $full_dependencies) {
        $fully_qualified_dependency = "${dependency}:$triplet"
        if ($vcpkg_installed -Contains $fully_qualified_dependency -eq $false) {
            $vcpkg_install_dependency = "$vcpkg install $fully_qualified_dependency --recurse"
            Invoke-Expression $vcpkg_install_dependency
        }
    }
}

# Install dependencies with vcpkg
if ($installDependencies) {
    $vcpkg = "vcpkg.exe"
    Write-Host "VCPKG INSTALL"
    if (Get-Command $vcpkg -ErrorAction SilentlyContinue) {
        $triplet = "x64-windows"
        if ($platform -eq "Win32") {
            $triplet = "x86-windows"
        }

        Install-VcpkgDependencies -triplet $triplet -dependencies $dependencies
    }
    else {
        Write-Host "$vcpkg not found"
        exit
    }
}

$vswhere_path = "vswhere.exe"

if (!(Get-Command $vswhere_path -ErrorAction SilentlyContinue)) {
    # Try to find vswhere in visual studio install location
    $vswhere_path = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe";
    Write-Output $vswhere_path
    if (!(Test-Path -LiteralPath $vswhere_path -PathType Leaf)) {
        # Try to find in chocolatey install location
        $vswhere_path = "${env:ProgramData}\chocolatey\lib\vswhere\tools\vswhere.exe";
    }
    if (!(Test-Path -LiteralPath $vswhere_path -PathType Leaf)) {
        # Give up
        Write-Output "Unable to find vswhere.exe";
        exit;
    }
}

# configure vswhere parameters
$vswhere_version = "-latest"
if ($toolset -ne "latest") {
    $vswhere_version = "-version {0}" -f $toolset_version_from_product_line[$toolset]
}
$vswhere_cmd = "& ""$vswhere_path"" $vswhere_version -requires Microsoft.Component.MSBuild"

# Get the visual studio product line version number (i.e. 2010, 2012, 2013, 2015, 2017, 2019, etc...)
# Also determine if an appropriate visual studio is installed.
$find_vs_version_cmd = "$vswhere_cmd -property catalog_productLineVersion"
$vs_version = Invoke-Expression $find_vs_version_cmd
Write-Output "Using vswhere.exe located at ""$vswhere_path""";

if ($null -eq $vs_version) {
    # Error: no installatios found, exit.
    Write-Host "No visual studio installation found. Install the MVC++ build tools."
    exit
} else {
    Write-Host "Using vs$vs_version toolset.";
}


$steps = $build_action_steps[$buildAction];

if ($steps.do_build) {
    if (!$skipPremake) {
        $premake5_exe = "premake5"
        Invoke-Expression "$premake5_exe vs$vs_version"
    }
    
    # Find MSBuild
    $find_ms_build_cmd = "$vswhere_cmd -find MSBuild\**\Bin\MSBuild.exe"
    $ms_build = Invoke-Expression $find_ms_build_cmd
    Write-Host "Using MSBuild.exe located at ""$ms_build"""
    
    # Configure MSBuild parameters
    $ms_build_action = $ms_build_actions[$buildAction]
    $ms_build_cmd = """$ms_build"" /t:$ms_build_action bin/$target.sln /p:Platform=$platform /p:Configuration=$configuration /m"

    Write-Host "Building With:\n $ms_build_cmd";

    # build project files
    Invoke-Expression "& $ms_build_cmd"
}

if ($steps.do_clean) {
    # If we're cleaning manually call the asset_pipeline clean
    #.\asset_pipeline.ps1 -target $target -platform $platform -configuration $configuration -clean
}

if ($steps.do_run) {
    Invoke-Expression "./bin/$target/bin/$platform/$configuration/$target.exe"
}