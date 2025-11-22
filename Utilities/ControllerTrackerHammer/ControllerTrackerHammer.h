#pragma once
#if 0

How to use ControllerTrackerHammer (3/14/2013):
(1) Get yourself a text file containing a SignedTicket. Here's one, but if it's way in the future from now, it may have changed some:

{
	Tickettext <&\r\n{\r\nAccountID 52\r\nAccountName awerner\r\nDisplayName awerner\r\nPwaccountname abwerner\r\nUexpirationtime 416522157\r\n\r\nPermissions\r\n{\r\nProductName Night\r\nPermissionstring "shard: test;Alpha_RemoveXPLimit: 1"\r\nAccessLevel 9\r\n}\r\n\r\nPermissions\r\n{\r\nProductName Night\r\nPermissionstring "shard: qalocal2;Alpha_RemoveXPLimit: 1"\r\nAccessLevel 9\r\n}\r\n\r\nPermissions\r\n{\r\nProductName Night\r\nPermissionstring "shard: qalocal;Alpha_RemoveXPLimit: 1"\r\nAccessLevel 9\r\n}\r\n\r\nPermissions\r\n{\r\nProductName Night\r\nPermissionstring "shard: qa;Alpha_RemoveXPLimit: 1"\r\nAccessLevel 9\r\n}\r\n\r\nPermissions\r\n{\r\nProductName StarTrek\r\nPermissionstring "shard: qalocal2;AllBasicLevels: 1;AllBasicZones: 1;AllSocial: 1;AllTrade: 1;LoginAllowed: 1;Premium: 1;Standard: 1;token: retail;Upgraded: 1"\r\nAccessLevel 9\r\n}\r\n\r\nPermissions\r\n{\r\nProductName StarTrek\r\nPermissionstring "shard: qalocal;AllBasicLevels: 1;AllBasicZones: 1;AllSocial: 1;AllTrade: 1;LoginAllowed: 1;Premium: 1;Standard: 1;token: retail;Upgraded: 1"\r\nAccessLevel 9\r\n}\r\n\r\nPermissions\r\n{\r\nProductName StarTrek\r\nPermissionstring "shard: qa;AllBasicLevels: 1;AllBasicZones: 1;AllSocial: 1;AllTrade: 1;LoginAllowed: 1;Premium: 1;Standard: 1;token: retail;Upgraded: 1"\r\nAccessLevel 9\r\n}\r\n\r\nLogintype CrypticAndPW\r\n}&>
	Uexpirationtime 436522157
	Strtickettpi <&\r\n\r\n	Staticdefinelist Accountlogintype\r\n	{\r\n		Type IntDefine\r\n		Define Default 0\r\n		Define Cryptic 1\r\n		Define PerfectWorld 2\r\n		Define CrypticAndPW 3\r\n		Define Max 4\r\n	}\r\n\r\n	Parseinfo Accountticket\r\n	{\r\n		Column NAME( Accountticket ), IGNORE, PARSETABLE_INFO\r\n		Column NAME( { ), START\r\n		Column NAME( AccountID ), INT\r\n		Column NAME( AccountName ), FIXEDSTRING, STRING_LENGTH( 128 )\r\n		Column NAME( DisplayName ), FIXEDSTRING, STRING_LENGTH( 128 )\r\n		Column NAME( Pwaccountname ), STRING\r\n		Column NAME( Uexpirationtime ), INT\r\n		Column NAME( Permissions ), STRUCT, SUBTABLE( Accountticket.Permissions )\r\n		Column NAME( Usalt ), INT\r\n		Column NAME( Invaliddisplayname ), U8\r\n		Column NAME( Machinerestricted ), U8\r\n		Column NAME( Savingnext ), U8\r\n		Column NAME( Logintype ), INT, STATICDEFINELIST( Accountlogintype )\r\n		Column NAME( } ), END\r\n	}\r\n\r\n	Parseinfo Accountticket.Permissions\r\n	{\r\n		Column NAME( Accountpermissionstruct ), IGNORE, PARSETABLE_INFO\r\n		Column NAME( { ), START\r\n		Column NAME( ProductName ), STRING, ESTRING\r\n		Column NAME( Permissionstring ), STRING, ESTRING\r\n		Column NAME( AccessLevel ), INT\r\n		Column NAME( Uflags ), INT\r\n		Column NAME( } ), END\r\n	}\r\n&>
	Uticketcrc 55445916
}

(2) Launch a local standalone logserver (there's a batch file in c:\core\tools\bin to do so)
(3) Launch NewControllerTracker.exe with these command line args: -forceAccountTicket c:\temp\testticket.txt -DumpStats -logserver localhost
  (obviously replace c:\temp\testticket.txt with the filename of your text file)
(4) Create some permanent shards on your controllerTracker. The easiest way is to just hand-edit the 
  text file c:\core\localdata\ControllerTrackerStaticData.txt, and put something like this in it (then restart CT):

{

Permanentshards
{
	Name Bart

	Basicinfo
	{
		Monitoringlink <&<a href="http://172.31.99.25/viewxpath"\>Monitor</a\>&>
		Shardcategoryname TEST
		ProductName Night
		shardName Bart
		Clustername AlexCluster
		Shardloginserveraddress 172.31.99.25
		Shardcontrolleraddress 172.31.99.25
		Versionstring 139826
		Patchcommandline "-sync -project NightServer -server patchinternal -port 7255 -name NW_1_20121031_0516"
		Autoclientcommandline " -SuperEsc SetShardInfoString Night_shardQcomma_name_QlparensBartQrparensQcomma_category_QlparensTestQrparensQcomma_version_Qlparens139826QrparensQcomma_machine_QlparensAWERNERQrparensQcomma_start_time_Qlparens2012Qdash10Qdash31_16Qcolon24Qcolon46Qrparens -SetAccountServer 172.31.99.25"
		Prepatchcommandline "-sync -project NightServer -server patchinternal -port 7255 -name ABCD_1234"
		Uniqueid 1
		Haslocalmontiringmcp 1
		Notreallythere 1
		Accountserver localHost
	}
}

Permanentshards
{
	Name Lisa

	Basicinfo
	{
		Monitoringlink <&<a href="http://172.31.99.27/viewxpath"\>Monitor</a\>&>
		Shardcategoryname TEST
		ProductName Night
		shardName Lisa
		Clustername AlexCluster
		Shardloginserveraddress 172.31.99.27
		Shardcontrolleraddress 172.31.99.27
		Versionstring 139826
		Patchcommandline "-sync -project NightServer -server patchinternal -port 7255 -name NW_1_20121031_0516"
		Autoclientcommandline " -SuperEsc SetShardInfoString Night_shardQcomma_name_QlparensLisaQrparensQcomma_category_QlparensTestQrparensQcomma_version_Qlparens139826QrparensQcomma_machine_QlparensAWERNERQdash2QrparensQcomma_start_time_Qlparens2012Qdash10Qdash31_15Qcolon05Qcolon26Qrparens -SetAccountServer 172.31.99.27"
		Prepatchcommandline "-sync -project NightServer -server patchinternal -port 7255 -name ABCD_1234"
		Uniqueid 2
		Haslocalmontiringmcp 1
		Allloginserverips  459481004
		Notreallythere 1
		Accountserver localHost
	}
}

Permanentshards
{
	Name Homer

	Basicinfo
	{
		Monitoringlink <&<a href="http://172.31.99.27/viewxpath"\>Monitor</a\>&>
		Shardcategoryname TEST
		ProductName Night
		shardName Homer
		Shardloginserveraddress 172.31.99.27
		Shardcontrolleraddress 172.31.99.27
		Versionstring 139826
		Patchcommandline "-sync -project NightServer -server patchinternal -port 7255 -name NW_1_20121031_0516"
		Autoclientcommandline " -SuperEsc SetShardInfoString Night_shardQcomma_name_QlparensLisaQrparensQcomma_category_QlparensTestQrparensQcomma_version_Qlparens139826QrparensQcomma_machine_QlparensAWERNERQdash2QrparensQcomma_start_time_Qlparens2012Qdash10Qdash31_15Qcolon05Qcolon26Qrparens -SetAccountServer 172.31.99.27"
		Prepatchcommandline "-sync -project NightServer -server patchinternal -port 7255 -name ABCD_1234"
		Uniqueid 2
		Haslocalmontiringmcp 1
		Allloginserverips  459481004
		Notreallythere 1
		Accountserver localHost
	}
}


Permanentshards
{
	Name Wacko

	Basicinfo
	{
		Monitoringlink <&<a href="http://172.31.99.25/viewxpath"\>Monitor</a\>&>
		Shardcategoryname TEST
		ProductName StarTrek
		shardName Wacko
		Clustername STOCluster
		Shardloginserveraddress 172.31.99.25
		Shardcontrolleraddress 172.31.99.25
		Versionstring 139826
		Patchcommandline "-sync -project NightServer -server patchinternal -port 7255 -name NW_1_20121031_0516"
		Autoclientcommandline " -SuperEsc SetShardInfoString Night_shardQcomma_name_QlparensBartQrparensQcomma_category_QlparensTestQrparensQcomma_version_Qlparens139826QrparensQcomma_machine_QlparensAWERNERQrparensQcomma_start_time_Qlparens2012Qdash10Qdash31_16Qcolon24Qcolon46Qrparens -SetAccountServer 172.31.99.25"
		Prepatchcommandline "-sync -project NightServer -server patchinternal -port 7255 -name ABCD_1234"
		Uniqueid 1
		Haslocalmontiringmcp 1
		Notreallythere 1
		Accountserver localHost
	}
}

Permanentshards
{
	Name Jacko

	Basicinfo
	{
		Monitoringlink <&<a href="http://172.31.99.27/viewxpath"\>Monitor</a\>&>
		Shardcategoryname TEST
		ProductName StarTrek
		shardName Jacko
		Clustername STOCluster
		Shardloginserveraddress 172.31.99.27
		Shardcontrolleraddress 172.31.99.27
		Versionstring 139826
		Patchcommandline "-sync -project NightServer -server patchinternal -port 7255 -name NW_1_20121031_0516"
		Autoclientcommandline " -SuperEsc SetShardInfoString Night_shardQcomma_name_QlparensLisaQrparensQcomma_category_QlparensTestQrparensQcomma_version_Qlparens139826QrparensQcomma_machine_QlparensAWERNERQdash2QrparensQcomma_start_time_Qlparens2012Qdash10Qdash31_15Qcolon05Qcolon26Qrparens -SetAccountServer 172.31.99.27"
		Prepatchcommandline "-sync -project NightServer -server patchinternal -port 7255 -name ABCD_1234"
		Uniqueid 2
		Haslocalmontiringmcp 1
		Allloginserverips  459481004
		Notreallythere 1
		Accountserver localHost
	}
}

Permanentshards
{
	Name Blacko

	Basicinfo
	{
		Monitoringlink <&<a href="http://172.31.99.27/viewxpath"\>Monitor</a\>&>
		Shardcategoryname TEST
		ProductName StarTrek
		shardName Blacko
		Shardloginserveraddress 172.31.99.27
		Shardcontrolleraddress 172.31.99.27
		Versionstring 139826
		Patchcommandline "-sync -project NightServer -server patchinternal -port 7255 -name NW_1_20121031_0516"
		Autoclientcommandline " -SuperEsc SetShardInfoString Night_shardQcomma_name_QlparensLisaQrparensQcomma_category_QlparensTestQrparensQcomma_version_Qlparens139826QrparensQcomma_machine_QlparensAWERNERQdash2QrparensQcomma_start_time_Qlparens2012Qdash10Qdash31_15Qcolon05Qcolon26Qrparens -SetAccountServer 172.31.99.27"
		Prepatchcommandline "-sync -project NightServer -server patchinternal -port 7255 -name ABCD_1234"
		Uniqueid 2
		Haslocalmontiringmcp 1
		Allloginserverips  459481004
		Notreallythere 1
		Accountserver localHost
	}
}

}


(5) Now you are ready to do some ControllerTrackerHammering. Each copy of ControllerTrackerHammer will generate about 70
	ticket requests per second. So run one, on a different machine, using the -CTName arg to set the machine name of the machine
	where the CT is running. You should see that the console of both the CTHammer and the CT are reporting how many tickets are being
	processed each second. Keep running more CTHammers until the CT gets overloaded and the framerate drops precipitously.

(6) If you run too many CTHammers on one machine at once, they might start running out of virtual dynamic ports. On newer versions
    of Windows, you can alleviate that by runnin
	  netsh int ipv4 set dynamicport tcp start=10000 num=50000

#endif