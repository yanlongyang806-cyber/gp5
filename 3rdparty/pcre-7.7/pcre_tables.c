<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|Win32">
      <Configuration>Debug</Configuration>
      <Platform>Win32</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="Full Debug|Win32">
      <Configuration>Full Debug</Configuration>
      <Platform>Win32</Platform>
    </ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <ProjectGuid>{858253C9-BA8C-4A01-B96C-107286307825}</ProjectGuid>
    <RootNamespace>POToCSV</RootNamespace>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Debug|Win32'" Label="Configuration">
    <ConfigurationType>Application</ConfigurationType>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Full Debug|Win32'" Label="Configuration">
    <ConfigurationType>Application</ConfigurationType>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  <ImportGroup Condition="'$(Configuration)|$(Platform)'=='Debug|Win32'" Label="PropertySheets">
    <Import Project="..\..\PropertySheets\GeneralSettings.props" />
    <Import Project="..\..\PropertySheets\CrypticApplication.props" />
    <Import Project="..\..\PropertySheets\LinkerOptimizations.props" />
  </ImportGroup>
  <ImportGroup Condition="'$(Configuration)|$(Platform)'=='Full Debug|Win32'" Label="PropertySheets">
    <Import Project="..\..\PropertySheets\GeneralSettings.props" />
    <Import Project="..\..\PropertySheets\CrypticApplication.props" />
    <Import Project="..\..\PropertySheets\LinkerOptimizations.props" />
  </ImportGroup>
  <ImportGroup Label="ExtensionSettings">
  </ImportGroup>
  <PropertyGroup Label="UserMacros" />
  <PropertyGroup>
    <_ProjectFileVersion>10.0.30319.1</_ProjectFileVersion>
    <TargetName Condition="'$(Configuration)|$(Platform)'=='Full Debug|Win32'">$(ProjectName)FD</TargetName>
  </PropertyGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Debug|Win32'">
    <ClCompile>
      <AdditionalIncludeDirectories>../../3rdparty/xdk/win32;../../libs/HostIOLib;../../libs/ServerLib\pub;../../3rdparty/zlib;../common/utils;../common/components;../common/network;../common;../../libs/Common;../../libs/HttpLib;$(VSINSTALLDIR)\DIA SDK\include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <PreprocessorDefinitions>WIN32;_DEBUG;_CONSOLE;_CRTDBG_MAP_ALLOC=1;AUX_SERVER_DEFINE;%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
    <Link>
      <AdditionalOptions>/MACHINE:I386 %(AdditionalOptions)</AdditionalOptions>
      <AdditionalDependencies>psapi.lib;Rpcrt4.lib;Mswsock.lib;apr-1.lib;apriconv-1.lib;aprutil-1.lib;libsvn_client-1.lib;libsvn_delta-1.lib;libsvn_diff-1.lib;libsvn_fs-1.lib;libsvn_fs_fs-1.lib;libsvn_fs_util-1.lib;libsvn_ra-1.lib;libsvn_ra_local-1.lib;libsvn_ra_svn-1.lib;libsvn_repos-1.lib;libsvn_subr-1.lib;libsvn_test-1.lib;libsvn_wc-1.lib;xml.lib;%(AdditionalDependencies)</AdditionalDependencies>
      <ShowProgress>NotSet</ShowProgress>
      <AdditionalLibraryDirectories>..\..\3rdparty\bin;../../3rdparty/xdk/win32lib;..\..\3rdparty\subversion-1.6.6\output\win32\libs;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <ModuleDefinitionFile>
      </ModuleDefinitionFile>
      <SubSystem>Console</SubSystem>
    </Link>
    <PreBuildEvent>
      <Command>..\..\utilities\bin\structparser X $(ProjectDir) X $(ProjectFileName) X $(Platform) X $(Configuration) X $(VCInstallDir) X $(SolutionPath)</Command>
    </PreBuildEvent>
    <ResourceCompile>
      <PreprocessorDefinitions>_DEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <Culture>0x0409</Culture>
    </ResourceCompile>
  </ItemDefinitionGroupfineInt GenesisMultiExcludeRotTypeEnum[];

AUTO_ENUM;
typedef enum WorldDoorType
{
	WorldDoorType_None,					//Don't transfer the player anywhere	
	WorldDoorType_MapMove,				//Standard door type
	WorldDoorType_QueuedInstance,		//Queued pve instance, just pops up a UI to join a queue
	WorldDoorType_JoinTeammate,			//Allows players to transfer to teammates that have transferred through identically tagged doors
	WorldDoorType_Count,	EIGNORE
	WorldDoorType_Keyed,
} WorldDoorType;
extern StaticDefineInt WorldDoorTypeEnum[];

AUTO_ENUM;
typedef enum WorldEncounterRewardLevelType
{
	kWorldEncounterRewardLevelType_DefaultLevel = 0,
	kWorldEncounterRewardLevelType_PlayerLevel,
	kWorldEncounterRewardLevelType_SpecificLevel,
} WorldEncounterRewardLevelType;
extern StaticDefineInt WorldEncounterRewardLevelTypeEnum[];

AUTO_ENUM;
typedef enum WorldEncounterRewardType
{
	kWorldEncounterRewardType_DefaultRewards = 0,
	kWorldEncounterRewardType_OverrideStandardRewards,
	kWorldEncounterRewardType_AdditionalRewards,
} WorldEncounterRewardType;
extern StaticDefineInt WorldEncounterRewardTypeEnum[];

AUTO_ENUM;
typedef enum WorldEncounterSpawnCondType
{
	WorldEncounterSpawnCondType_Normal, 
	WorldEncounterSpawnCondType_RequiresPlayer, 
} WorldEncounterSpawnCondType;
extern StaticDefineInt WorldEncounterSpawnCondTypeEnum[];

AUTO_ENUM;
typedef enum WorldEncounterRadiusType
{
	WorldEncounterRadiusType_None,
	WorldEncounterRadiusType_Always,
	WorldEncounterRadiusType_Short, 
	WorldEncounterRadiusType_Medium, 
	WorldEncounterRadiusType_Long,
	WorldEncounterRadiusType_Custom
} WorldEncounterRadiusType;
extern StaticDefineInt WorldEncounterRadiusTypeEnum[];

AUTO_ENUM;
typedef enum WorldEncounterTimerType
{
	WorldEncounterTimerType_None,
	WorldEncounterTimerType_Never,
	WorldEncounterTimerType_Short, 
	WorldEncounterTimerType_Medium, 
	WorldEncounterTimerType_Long, 
	WorldEncounterTimerType_Custom, 
} WorldEncounterTimerType;
extern StaticDefineInt WorldEncounterTimerTypeEnum[];

AUTO_ENUM;
typedef enum WorldEncounterWaveTimerType
{
	WorldEncounterWaveTimerType_Short, 
	WorldEncounterWaveTimerType_Medium, 
	WorldEncounterWaveTimerType_Long, 
	WorldEncounterWaveTimerType_Immediate, 
	WorldEncounterWaveTimerType_Custom,		ENAMES(Custom None Never)
} WorldEncounterWaveTimerType;
extern StaticDefineInt WorldEncounterWaveTimerTypeEnum[];

AUTO_ENUM;
typedef enum WorldEncounterWaveDelayTimerType
{
	WorldEncounterWaveDelayTimerType_None, 
	WorldEncounterWaveDelayTimerType_Short, 
	WorldEncounterWaveDelayTimerType_Medium, 
	WorldEncounterWaveDelayTimerType_Long, 
	WorldEncounterWaveDelayTimerType_Custom,	ENAMES(Custom Never)
} WorldEncounterWaveDelayTimerType;
extern StaticDefineInt WorldEncounterWaveDelayTimerTypeEnum[];

AUTO_ENUM;
typedef enum WorldEncounterDynamicSpawnType
{
	WorldEncounterDynamicSpawnType_Default,
	WorldEncounterDynamicSpawnType_Static,
	WorldEncounterDynamicSpawnType_Dynamic,
} WorldEncounterDynamicSpawnType;
extern StaticDefineInt WorldEncounterDynamicSpawnTypeEnum[];

AUTO_ENUM;
typedef enum WorldEncounterSpawnTeamSize
{
	WorldEncounterSpawnTeamSize_NotForced = 0,
	WorldEncounterSpawnTeamSize_1 = 1,
	WorldEncounterSpawnTeamSize_2 = 2,
	WorldEncounterSpawnTeamSize_3 = 3,
	WorldEncounterSpawnTeamSize_4 = 4,
	WorldEncounterSpawnTeamSize_5 = 5,
} WorldEncounterSpawnTeamSize;
extern StaticDefineInt WorldEncounterSpawnTeamSizeEnum[];

AUTO_ENUM;
typedef enum WorldEncounterMastermindSpawnType
{
	WorldEncounterMastermindSpawnType_None,
	WorldEncounterMastermindSpawnType_StaticAllowRespawn,
	WorldEncounterMastermindSpawnType_DynamicOnly,
} WorldEncounterMastermindSpawnType;
extern StaticDefineInt WorldEncounterMastermindSpawnTypeEnum[];

AUTO_ENUM;
typedef enum WorldOptionalActionPriority
{
	WorldOptionalActionPriority_Low = 0,
	WorldOptionalActionPriority_Normal = 5,
	WorldOptionalActionPriority_High = 10,
	WorldOptionalActionPriority_Order_1 = 9,
	WorldOptionalActionPriority_Order_2 = 8,
	WorldOptionalActionPriority_Order_3 = 7,
	WorldOptionalActionPriority_Order_4 = 6,
	WorldOptionalActionPriority_Order_5 = 4,
	WorldOptionalActionPriority_Order_6 = 3,
	WorldOptionalActionPriority_Order_7 = 2,
	WorldOptionalActionPriority_Order_8 = 1,
} WorldOptionalActionPriority;
extern StaticDefineInt WorldOptionalActionPriorityEnum[];

AUTO_ENUM;
typedef enum WorldTerrainExclusionType
{
	WorldTerrainExclusionType_Anywhere,
	WorldTerrainExclusionType_Above_Terrain, 
	WorldTerrainExclusionType_Below_Terrain 
} WorldTerrainExclusionType;
extern StaticDefineInt WorldTerrainExclusionTypeEnum[];

//If you change this, you need to change WorldTerrainCollisionTypeUIEnum as well
AUTO_ENUM;
typedef enum WorldTerrainCollisionType
{
	WorldTerrainCollisionType_Collide_All,
	WorldTerrainCollisionType_Collide_All_Except_Paths,
	WorldTerrainCollisionType_Collide_None,
	WorldTerrainCollisionType_Collide_Simil