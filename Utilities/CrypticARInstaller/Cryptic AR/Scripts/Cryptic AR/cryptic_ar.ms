global crypt_main

global skins

global r_deltoid
global l_deltoid

global r_arm_ikcon
global l_arm_ikcon
global r_leg_ikcon
global l_leg_ikcon

global l_toeroll
global l_ballroll
global l_heelroll
global l_rock
global l_pivot
global l_knee
global l_toe

global r_toeroll
global r_ballroll
global r_heelroll
global r_rock
global r_pivot
global r_knee
global r_toe

global l_spread
global l_f1_base
global l_f1_tip
global l_f2_base
global l_f2_tip
global l_f3_base
global l_f3_tip

global l_f4_base
global l_f4_tip

global l_t_base
global l_t_tip
global l_t_roll

global r_spread
global r_f1_base
global r_f1_tip
global r_f2_base
global r_f2_tip
global r_f3_base
global r_f3_tip

global r_f4_base
global r_f4_tip

global r_t_base
global r_t_tip
global r_t_roll

global r_elbow
global l_elbow

global spin_temp
global spn_f1_basetmp
global spn_f1_tiptmp
global spn_f2_basetmp
global spn_f2_tiptmp
global spn_f3_basetmp
global spn_f3_tiptmp
global spn_f4_basetmp
global spn_f4_tiptmp

global num_poses = 0
global poses = #()
global pose_name = ""
global temp_array = #()
global file_name = "hands.pos"
	
global R_Arm_IK = 0
global L_Arm_IK = 0
global R_Leg_IK = 1
global L_Leg_IK = 1

global ni_rarm
global ni_larm
global ni_neck

global cryptSquash
global crypt_fingers
global cryptIso
global cryptScaling

--Scaling

global scl_shoulders
global scl_mass
global scl_hips
global scl_height
global scl_arms
global scl_legs
global scl_head
global scl_hands
global scl_feet
global scl_fat

global presets = #()
global numPresets = 0

function IK_L_Leg = (

	L_Leg_IK = 1
	crypt_main.btn_l_foot_ik.Caption = "IK"
	Hide #($FK_UlegL, $FK_LlegL, $FK_FootL, $FK_ToeL)
	unhide #($IK_Control_FootL)
	$IK_Control_FootL.FK_IK_L = 100

)

function IK_R_Leg = (

	R_Leg_IK = 1
	crypt_main.btn_r_foot_ik.Caption = "IK"
	Hide #($FK_UlegR, $FK_LlegR, $FK_FootR, $FK_ToeR)
	unhide #($IK_Control_FootR)
	$IK_Control_FootR.FK_IK_R = 100

)

function FK_L_Leg = (

	L_Leg_IK = 0
	crypt_main.btn_l_foot_ik.Caption = "FK"
	Hide #($IK_Control_FootL)
	unhide #($FK_UlegL, $FK_LlegL, $FK_FootL, $FK_ToeL)
	$IK_Control_FootL.FK_IK_L = 0

)

function FK_R_Leg = (

	R_Leg_IK = 0
	crypt_main.btn_r_foot_ik.Caption = "FK"
	Hide #($IK_Control_FootR)
	unhide #($FK_UlegR, $FK_LlegR, $FK_FootR, $FK_ToeR)
	$IK_Control_FootR.FK_IK_R = 0

)

function FK_L_Arm = (

	L_Arm_IK = 0
	crypt_main.btn_l_hand_ik.Caption = "FK"
	Hide #($IK_HandL)
	unhide #($FK_UarmL, $FK_LarmL, $FK_HandL)
	$IK_HandL.ArmL_FK_IK = 0

)

function FK_R_Arm = (

	R_Arm_IK = 0
	crypt_main.btn_r_hand_ik.Caption = "FK"
	Hide #($IK_HandR)
	unhide #($FK_UarmR, $FK_LarmR, $FK_HandR)
	$IK_HandR.ArmR_FK_IK = 0

)

function IK_L_Arm = (

	L_Arm_IK = 1
	crypt_main.btn_l_hand_ik.Caption = "IK"
	Hide #($FK_UarmL, $FK_LarmL, $FK_HandL)
	unhide #($IK_HandL)
	$IK_HandL.ArmL_FK_IK = 100

)

function IK_R_Arm = (

	R_Arm_IK = 1
	crypt_main.btn_r_hand_ik.Caption = "IK"
	Hide #($FK_UarmR, $FK_LarmR, $FK_HandR)
	unhide #($IK_HandR)
	$IK_HandR.ArmR_FK_IK = 100
	
)

function Hide_Skin name = (

	layer = layermanager.getLayerFromName name
	layer.isHidden = true
	layer.isFrozen = false

)

function Show_Skin name = (

	layer = layermanager.getLayerFromName name
	layer.isHidden = false
	layer.isFrozen = true
	crypt_main.btnFreezeSkin.Caption = "Unfreeze"

)

function Freeze_Skin name = (

	layer = layermanager.getLayerFromName name
	if layer.isFrozen == true then(
		
		layer.isFrozen = false
		crypt_main.btnFreezeSkin.Caption = "Freeze"
	
	)else(

		layer.isFrozen = true
		crypt_main.btnFreezeSkin.Caption = "Unfreeze"

	)
)

function R_Arm_NIOFF = (

	$Dummy_ArmR.rotation.controller.Orientation_Constraint.controller.weight[2] = 100
	$Dummy_ArmR.rotation.controller.Orientation_Constraint.controller.weight[1] = 0
	ni_rarm = 0

)

function R_Arm_NION = (

	$Dummy_ArmR.rotation.controller.Orientation_Constraint.controller.weight[2] = 0
	$Dummy_ArmR.rotation.controller.Orientation_Constraint.controller.weight[1] = 100
	ni_rarm = 1

)

function L_Arm_NIOFF = (

	$Dummy_ArmL.rotation.controller.Orientation_Constraint.controller.weight[2] = 100
	$Dummy_ArmL.rotation.controller.Orientation_Constraint.controller.weight[1] = 0
	ni_larm = 0

)

function L_ARM_NION = (

	$Dummy_ArmL.rotation.controller.Orientation_Constraint.controller.weight[2] = 0
	$Dummy_ArmL.rotation.controller.Orientation_Constraint.controller.weight[1] = 100
	ni_larm = 1

)

function Neck_NIOFF = (

	$Dummy_Neck.rotation.controller.Orientation_Constraint.controller.weight[2] = 100
	$Dummy_Neck.rotation.controller.Orientation_Constraint.controller.weight[1] = 0
	ni_neck = 0

)

function Neck_NION = (

	$Dummy_Neck.rotation.controller.Orientation_Constraint.controller.weight[2] = 0
	$Dummy_Neck.rotation.controller.Orientation_Constraint.controller.weight[1] = 100
	ni_neck = 1

)

include "crypt_iso.ms"
include "crypt_fingers.ms"
include "crypt_squash.ms"
include "crypt_ids.ms"
include "crypt_scaling.ms"

rollout crypt_main "Cryptic AR" width:404 height:628
(

	button btn_r_delt "R_Deltoid" pos:[107,8] width:65 height:25
	button btn_l_delt "L_Deltoid" pos:[227,8] width:65 height:25
	spinner spn_r_delt "Fix " pos:[120,40] width:50 height:16 range:[-180,180,0] controller:r_deltoid
	spinner spn_l_delt "Fix " pos:[240,40] width:50 height:16 range:[-180,180,0] controller:l_deltoid

	button btn_chest "Chest" pos:[151,94] width:97 height:40 enabled:true images:#("chest.jpg", undefined, 1, 1, 1, 1, 1)
	button btn_r_bicep "R_Bicep" pos:[110,94] width:41 height:49 enabled:true images:#("r_bicep.jpg", undefined, 1, 1, 1, 1, 1)
	button btn_head "Head" pos:[179,47] width:40 height:47 images:#("head.jpg", undefined, 1, 1, 1, 1, 1)
	button btn_r_elbow "R_Elbow" pos:[61,94] width:49 height:49 images:#("r_forearm.jpg", undefined, 1, 1, 1, 1, 1)
	button btn_r_hand "R_Hand" pos:[6,94] width:55 height:49 images:#("r_hand.jpg", undefined, 1, 1, 1, 1, 1)
	button btn_l_hand "L_Hand" pos:[342,94] width:52 height:49 images:#("l_hand.jpg", undefined, 1, 1, 1, 1, 1)
	button btn_l_elbow "L_Elbow" pos:[292,94] width:50 height:49 images:#("l_forearm.jpg", undefined, 1, 1, 1, 1, 1)
	button btn_l_bicep "L_Bicep" pos:[248,94] width:44 height:49 images:#("l_bicep.jpg", undefined, 1, 1, 1, 1, 1)
	button btn_ribs "Ribs" pos:[151,134] width:97 height:27 images:#("ribs.jpg", undefined, 1, 1, 1, 1, 1)
	button btn_waist "Waist" pos:[151,161] width:97 height:27 images:#("waist.jpg", undefined, 1, 1, 1, 1, 1)
	button btn_hips "Hips" pos:[151,188] width:97 height:36 enabled:true images:#("hips.jpg", undefined, 1, 1, 1, 1, 1) toolTip:""
	button btn_r_leg "Button" pos:[151,224] width:49 height:73 images:#("r_leg.jpg", undefined, 1, 1, 1, 1, 1)
	button btn_l_leg "Button" pos:[200,224] width:48 height:73 images:#("l_leg.jpg", undefined, 1, 1, 1, 1, 1)
	button btn_r_knee "Button" pos:[151,297] width:49 height:64 images:#("r_knee.jpg", undefined, 1, 1, 1, 1, 1) toolTip:""
	button btn_l_knee "Button" pos:[200,297] width:48 height:64 images:#("l_knee.jpg", undefined, 1, 1, 1, 1, 1)
	button btn_r_foot "Button" pos:[151,361] width:49 height:53 images:#("r_foot.jpg", undefined, 1, 1, 1, 1, 1)
	button btn_l_foot "Button" pos:[200,361] width:48 height:53 images:#("l_foot.jpg", undefined, 1, 1, 1, 1, 1) toolTip:""
	
	button btn_r_shoulder "R_Shoulder" pos:[107,60] width:65 height:32
	button btn_l_shoulder "L_Shoulder" pos:[227,60] width:65 height:32
	button btn_hip_rotation "Hip Rotation" pos:[78,189] width:70 height:29
	button btn_root "Root" pos:[3,189] width:70 height:29
	button btn_iso "Isolation" pos:[255,189] width:70 height:29
	button btn_squash "Squash" pos:[330,189] width:70 height:29
	button btn_neck "Neck" pos:[175,20] width:48 height:26
	label lbl1 "Toe Roll" pos:[16,225] width:58 height:17
	label lbl2 "Ball Roll" pos:[16,249] width:58 height:17
	label lbl3 "Heel Roll" pos:[16,296] width:56 height:17
	label lbl4 "Rock" pos:[16,320] width:48 height:17
	spinner spn_r_toeroll "" pos:[72,225] width:40 height:16 range:[-90,150,0] type:#float scale:0.1 controller:r_toeroll
	spinner spn_r_ballroll "" pos:[72,249] width:40 height:16 range:[0,100,0] controller:r_ballroll
	spinner spn_r_heelroll "" pos:[72,296] width:40 height:16 range:[0,100,0] controller:r_heelroll
	spinner spn_r_rock "" pos:[72,320] width:40 height:16 range:[-100,100,0] controller:r_rock
	label lbl5 "Pivot" pos:[16,344] width:48 height:17
	spinner spn_r_pivot "" pos:[72,344] width:40 height:16 range:[-180,180,0] controller:r_pivot
	label lbl6 "Knee" pos:[16,368] width:48 height:17
	spinner spn_r_knee "" pos:[72,368] width:40 height:16 range:[-300,300,0] controller:r_knee
	label lbl7 "Toe Roll" pos:[280,225] width:58 height:17
	label lbl8 "Ball Roll" pos:[280,249] width:58 height:17
	label lbl9 "Heel Roll" pos:[280,296] width:56 height:17
	label lbl10 "Rock" pos:[280,320] width:48 height:17
	spinner spn_l_toeroll "" pos:[336,225] width:40 height:16 range:[-90,150,0] scale:0.1 controller:l_toeroll
	spinner spn_l_ballroll "" pos:[336,249] width:40 height:16 range:[0,100,0] controller:l_ballroll
	spinner spn_l_heelroll "" pos:[336,296] width:40 height:16 range:[0,100,0] controller:l_heelroll
	spinner spn_l_rock "" pos:[336,320] width:40 height:16 range:[-100,100,0] controller:l_rock
	label lbl11 "Pivot" pos:[280,344] width:48 height:17
	spinner spn_l_pivot "" pos:[336,344] width:40 height:16 range:[-180,180,0] controller:l_pivot
	label lbl12 "Knee" pos:[280,368] width:48 height:17
	spinner spn_l_knee "" pos:[336,368] width:40 height:16 range:[-300,300,0] controller:l_knee
	button btn_r_fingers "Fingers" pos:[8,58] width:56 height:30
	button btn_r_foot_ik "IK" pos:[104,392] width:36 height:33 enabled:true
	button btn_l_foot_ik "IK" pos:[256,392] width:36 height:33 enabled:true
	button btn_l_toe "FK_L_Toe" pos:[306,392] width:75 height:33 enabled:true
	button btn_r_toe "FK_R_Toe" pos:[14,392] width:75 height:33 enabled:true
	button btn_r_hand_ik "FK" pos:[16,150] width:36 height:33 enabled:true
	button btn_l_hand_ik "FK" pos:[349,151] width:36 height:33 enabled:true
	
	spinner spn_r_elbow "Elbow" pos:[84,158] width:60 height:16 range:[-180,180,0] controller:r_elbow
	spinner spn_l_elbow "Elbow" pos:[280,158] width:60 height:16 range:[-180,180,0] controller:l_elbow
	
	button btn_rikhand "R_Arm" pos:[315,8] width:40 height:35
	button btn_likhand "L_Arm" pos:[357,8] width:40 height:35
	button btn_rikfoot "R_Leg" pos:[315,45] width:40 height:35
	button btn_likfoot "L_Leg" pos:[357,45] width:40 height:35
	
	label lbl165 "Toe" pos:[16,272] width:48 height:17
	spinner spn_r_toe "" pos:[72,272] width:40 height:16 range:[-60,100,0] controller:r_toe
	label lbl166 "Toe" pos:[280,272] width:48 height:17
	spinner spn_l_toe "" pos:[336,272] width:40 height:16 range:[-60,100,0] controller:l_toe
	button btn_all "Select All" pos:[8,8] width:80 height:22
	button btn_scale "Body Scaling" pos:[8,32] width:80 height:22
	
	
	-- Adjust Up
	groupBox grp1a "No Influence Objects" pos:[162,461] width:230 height:64
	button btn_neckni "On" pos:[245,493] width:64 height:24
	button btn_rarmni "On" pos:[170,493] width:64 height:24
	button btn_larmni "On" pos:[320,493] width:64 height:24
	label lbl1a "Right Arm" pos:[170,477] width:64 height:16
	label lbl2a "Neck" pos:[245,477] width:64 height:16
	label lbl3a "Left Arm" pos:[320,477] width:64 height:16
	
	spinner spn_rh_ik "r_handik" pos:[38,440] width:60 height:16 range:[0,100,0] controller:r_arm_ikcon
	spinner spn_lh_ik "l_handik" pos:[138,440] width:60 height:16 range:[0,100,0] controller:l_arm_ikcon
	spinner spn_rf_ik "r_footik" pos:[238,440] width:60 height:16 range:[0,100,0] controller:r_leg_ikcon
	spinner spn_lf_ik "l_footik" pos:[338,440] width:60 height:16 range:[0,100,0] controller:l_leg_ikcon
	
	dropDownList skinList "Display Mode" pos:[10,459] width:145 height:40 
	button btnFreezeSkin "Freeze" pos:[10,502] width:70 height:24
	button btnAdvancedSkin "Advanced" pos:[82,502] width:70 height:24
	
	button bmpBanner "Banner" pos:[2,530] width:400 height:100 images:#("banner.png", undefined, 1, 1, 1, 1, 1)
	
	on btn_r_delt pressed  do(
		if keyboard.controlPressed == false then clearselection()
		selectMore $Deltoid_R)
	on btn_l_delt pressed  do(
		if keyboard.controlPressed == false then clearselection()
		selectMore $Deltoid_L)
	on btn_chest pressed  do(
		if keyboard.controlPressed == false then clearselection()
		selectMore $FK_Chest)
	on btn_head pressed  do(
		if keyboard.controlPressed == false then clearselection()
		selectMore $FK_Head)
	on btn_ribs pressed  do(
		if keyboard.controlPressed == false then clearselection()
		selectMore $FK_Ribs)
	on btn_waist pressed  do(
		if keyboard.controlPressed == false then clearselection()
		selectMore $FK_Waist)
	on btn_hips pressed  do(
		if keyboard.controlPressed == false then clearselection()
		selectMore $Center_root)
	on btn_r_shoulder pressed  do(
		if keyboard.controlPressed == false then clearselection()
		selectMore $FK_Col_R)
	on btn_l_shoulder pressed  do(
		if keyboard.controlPressed == false then clearselection()
		selectMore $FK_Col_L)
	on btn_neck pressed  do(
		if keyboard.controlPressed == false then clearselection()
		selectMore $FK_Neck)
	on btn_rikhand pressed do(
		if keyboard.controlPressed == false then clearselection()
		selectMore $IK_HandR)
	on btn_likhand pressed do(
		if keyboard.controlPressed == false then clearselection()
		selectMore $IK_HandL)
	on btn_rikfoot pressed do(
		if keyboard.controlPressed == false then clearselection()
		selectMore $IK_Control_FootR)
	on btn_likfoot pressed do(
		if keyboard.controlPressed == false then clearselection()
		selectMore $IK_Control_FootL)
	
	on btn_r_fingers pressed  do
(	
		main_pos = GetDialogPos(crypt_main)
		main_pos[1] = main_pos[1] + 410
		DestroyDialog crypt_fingers
		createDialog crypt_fingers pos:main_pos width:448 height:382
	)
	on btn_r_foot_ik pressed  do
(
		if R_Leg_IK == 1 then(
		FK_R_Leg()
		)else(
		IK_R_Leg()
		)
	)
	on btn_l_foot_ik pressed  do
(
		if L_Leg_IK == 1 then(
		FK_L_Leg()
		)else(
		IK_L_Leg()
		)
	)
	on btn_r_hand_ik pressed  do
(
		if R_Arm_IK == 1 then(
		FK_R_Arm()
		)else(
		IK_R_Arm()
		)
	)
	on btn_l_hand_ik pressed  do
(
		if L_Arm_IK == 1 then(
		FK_L_Arm()
		)else(
		IK_L_Arm()
		)
	)
	on btn_r_bicep pressed  do
(
		if keyboard.controlPressed == false then clearselection()
		if R_Arm_IK == 0 then(
		selectMore $FK_UarmR
		)else(
		selectMore $IK_HandR
		)
	)
	on btn_r_elbow pressed  do
(
		if keyboard.controlPressed == false then clearselection()
		if R_Arm_IK == 0 then(
		selectMore $FK_LarmR
		)else(
		selectMore $IK_HandR
		)
	)
	on btn_r_hand pressed  do
(
		if keyboard.controlPressed == false then clearselection()
		if R_Arm_IK == 0 then(
		selectMore $FK_HandR
		)else(
		selectMore $IK_HandR
		)
	)
	on btn_l_bicep pressed  do
(
		if keyboard.controlPressed == false then clearselection()
		if L_Arm_IK == 0 then(
		selectMore $FK_UarmL
		)else(
		selectMore $IK_HandL
		)
	)
	on btn_l_elbow pressed  do
(
		if keyboard.controlPressed == false then clearselection()
		if L_Arm_IK == 0 then(
		selectMore $FK_LarmL
		)else(
		selectMore $IK_HandL
		)
	)
	on btn_l_hand pressed  do
(
		if keyboard.controlPressed == false then clearselection()
		if L_Arm_IK == 0 then(
		selectMore $FK_HandL
		)else(
		selectMore $IK_HandL
		)
	)
	on btn_l_leg pressed  do
(
		if keyboard.controlPressed == false then clearselection()
		if L_Leg_IK == 0 then(
		selectMore $FK_UlegL
		)else(
		selectMore $IK_Control_FootL
		)
	)
	on btn_l_knee pressed  do
(
		if keyboard.controlPressed == false then clearselection()
		if L_Leg_IK == 0 then(
		selectMore $FK_LlegL
		)else(
		selectMore $IK_Control_FootL
		)
	)
	on btn_l_foot pressed  do
(
		if keyboard.controlPressed == false then clearselection()
		if L_Leg_IK == 0 then(
		selectMore $FK_FootL
		)else(
		selectMore $IK_Control_FootL
		)
	)
	on btn_r_leg pressed  do
(
		if keyboard.controlPressed == false then clearselection()
		if R_Leg_IK == 0 then(
		selectMore $FK_UlegR
		)else(
		selectMore $IK_Control_FootR
		)
	)
	on btn_r_knee pressed  do
(
		if keyboard.controlPressed == false then clearselection()
		if R_Leg_IK == 0 then(
		selectMore $FK_LlegR
		)else(
		selectMore $IK_Control_FootR
		)
	)
	on btn_r_foot pressed  do
(
		if keyboard.controlPressed == false then clearselection()
		if R_Leg_IK == 0 then(
		selectMore $FK_FootR
		)else(
		selectMore $IK_Control_FootR
		)
	)
	on btn_hip_rotation pressed do
(
		if keyboard.controlPressed == false then clearselection()
		selectMore $FK_Hips
	)
	on btn_root pressed do(
	
		if keyboard.controlPressed == false then clearselection()
		selectMore $Root
	
	)
	on btn_r_toe pressed do
(
		if keyboard.controlPressed == false then clearselection()
		selectMore $FK_ToeR
	)
	on btn_l_toe pressed do
(
		if keyboard.controlPressed == false then clearselection()
		selectMore $FK_ToeL
	)
	on btn_scale pressed do
	(
	
		DestroyDialog cryptScaling
		createDialog cryptScaling width:472 height:528
	
	)
	on btn_all pressed do
	(
	
		if keyboard.controlPressed == false then clearselection()
		
		selectMore $Root
		selectMore $Center_root
		selectMore $FK_FootR
		selectMore $FK_ToeR
		selectMore $IK_Control_FootR
		selectMore $FK_LlegR
		selectMore $IK_Control_FootR
		selectMore $FK_UlegR
		selectMore $IK_Control_FootR
		selectMore $FK_FootL
		selectMore $FK_ToeL
		selectMore $IK_Control_FootL
		selectMore $FK_LlegL
		selectMore $IK_Control_FootL
		selectMore $FK_UlegL
		selectMore $IK_Control_FootL
		selectMore $FK_HandL
		selectMore $IK_HandL
		selectMore $FK_LarmL
		selectMore $IK_HandL
		selectMore $FK_UarmL
		selectMore $IK_HandL
		selectMore $FK_HandR
		selectMore $IK_HandR
		selectMore $FK_LarmR
		selectMore $IK_HandR
		selectMore $FK_UarmR
		selectMore $IK_HandR
		selectMore $FK_Chest
		
		selectMore $FK_Head
		
		selectMore $FK_Ribs
		
		selectMore $FK_Waist
		
		selectMore $FK_Hips
		
		selectMore $FK_Col_R
		
		selectMore $FK_Col_L
		
		selectMore $FK_Neck
		
		selectMore $Control_Fing_R
		
		selectMore $Control_Fing_L
		
		selectMore $Deltoid_R
		
		selectMore $Deltoid_L
		
	)	
	on btn_neckni pressed  do
	(
		if ni_neck == 0 then(
		
		Neck_NION()
		btn_neckni.Caption = "On"
		
		)else(
		
		Neck_NIOFF()
		btn_neckni.Caption = "Off"
		
		)
	)
	
	on btn_rarmni pressed  do
	(
		if ni_rarm == 0 then(
		
		R_Arm_NION()
		btn_rarmni.Caption = "On"
		
		)else(
		
		R_Arm_NIOFF()
		btn_rarmni.Caption = "Off"
		
		)
	)
	on btn_larmni pressed  do
	(
		if ni_larm == 0 then(
		
		L_Arm_NION()
		btn_larmni.Caption = "On"
		
		)else(
		
		L_Arm_NIOFF()
		btn_larmni.Caption = "Off"
		
		)
	)
	on btn_iso pressed do(
	
		DestroyDialog cryptIso

		createDialog cryptIso		
	
	)
	on btn_squash pressed do(
	
		DestroyDialog cryptSquash

		createDialog cryptSquash
	
	)	
	on crypt_main open do(
				
		skinList.items = skins
		
		for o in skins do(
	
			temp_layer = layermanager.getLayerFromName o
			if not temp_layer.ishidden then skinList.selection = findItem skins o
	
		)
		
		if $Dummy_Neck.rotation.controller.Orientation_Constraint.controller.weight[2] == 0 then(
		
			ni_neck = 1
			btn_neckni.Caption = "On"
		
		)else(
		
			ni_neck = 0
			btn_neckni.Caption = "Off"
			
		)
		
		if $Dummy_ArmR.rotation.controller.Orientation_Constraint.controller.weight[2] == 0 then(
		
			ni_rarm = 1
			btn_rarmni.Caption = "On"
		
		)else(
		
			ni_rarm = 0
			btn_rarmni.Caption = "Off"
			
		)
		
		if $Dummy_ArmL.rotation.controller.Orientation_Constraint.controller.weight[2] == 0 then(
		
			ni_larm = 1
			btn_larmni.Caption = "On"
		
		)else(
		
			ni_larm = 0
			btn_larmni.Caption = "Off"
			
		)
	
	)
	on crypt_main close do(
	
		callbacks.removeScripts #filePostOpen

	)
	on skinList selected n do(
	
		for s in skins do(
		
			Hide_Skin s
		
		)
		Show_Skin skinList.selected
		
		layer = layermanager.GetLayerFromName skinList.selected
		
		layerRT = layer.layerAsRefTarg
	
		for o in refs.dependents layerRT do(
	
			if classOf o == "ExposeTm" then(
				try(
					unhide o
					unfreeze o
				)catch()
			)
		)
				
	)
	on btnFreezeSkin pressed do (
	
		Freeze_Skin skinList.selected
	
	)
	on btnAdvancedSkin pressed do (
	
		DestroyDialog cryptIDs

		createDialog cryptIDs
		
	)
	on bmpBanner pressed do (
	
		ShellLaunch "http://www.crypticar.com/" ""
	
	)
)

function Init = (

	temp_layer = layermanager.getLayer 0
	
	temp_layer.current = true
	
	if $Props_IDs == undefined then Dummy name:"Props_IDs" ishidden:true
	if $Props_Scale == undefined then Dummy name:"Props_Scale" ishidden:true
	if $Props_Fingers == undefined then Dummy name:"Props_Fingers" ishidden:true
	
	if layermanager.getLayerFromName "Corey" == undefined then Upgrade()
	
	if GetUserProp $Props_IDs "SkinString" == undefined or GetUserProp $Props_IDs "SkinString" == "" then SetUserProp $Props_IDs "SkinString" "Corey*EX_Bones"
	
	skin_str_temp = GetUserProp $Props_IDs "SkinString"
	skinstream = skin_str_temp as stringStream
	
	skins = #()
	
	while not eof skinstream do(
	
		append skins (readDelimitedString skinstream "*")
	
	)
	
	global l_toeroll = $IK_Control_FootL.baseObject.Custom_Attributes.L_toe_roll.Controller
	global l_ballroll = $IK_Control_FootL.baseObject.Custom_Attributes.L_ball_roll.Controller
	global l_heelroll = $IK_Control_FootL.baseObject.Custom_Attributes.L_heel_roll.Controller
	global l_rock = $IK_Control_FootL.baseObject.Custom_Attributes.L_rock.Controller
	global l_pivot = $IK_Control_FootL.baseObject.Custom_Attributes.L_twist.Controller
	global l_knee = $IK_Control_FootL.baseObject.Custom_Attributes.L_knee_swivel.Controller
	global l_toe = $IK_Control_FootL.baseObject.Custom_Attributes.L_toe.Controller
	
	global r_toeroll = $IK_Control_FootR.baseObject.Custom_Attributes.R_toe_roll.Controller
	global r_ballroll = $IK_Control_FootR.baseObject.Custom_Attributes.R_ball_roll.Controller
	global r_heelroll = $IK_Control_FootR.baseObject.Custom_Attributes.R_heel_roll.Controller
	global r_rock = $IK_Control_FootR.baseObject.Custom_Attributes.R_rock.Controller
	global r_pivot = $IK_Control_FootR.baseObject.Custom_Attributes.R_twist.Controller
	global r_knee = $IK_Control_FootR.baseObject.Custom_Attributes.R_knee_swivel.Controller
	global r_toe = $IK_Control_FootR.baseObject.Custom_Attributes.R_toe.Controller
	
	global l_spread = $Control_Fing_L.baseObject.Custom_Attributes.FL_spread.Controller
	global l_f1_base = $Control_Fing_L.baseObject.Custom_Attributes.FL_1_base.Controller
	global l_f1_tip = $Control_Fing_L.baseObject.Custom_Attributes.FL_1_tip.Controller
	global l_f2_base = $Control_Fing_L.baseObject.Custom_Attributes.FL_2_base.Controller
	global l_f2_tip = $Control_Fing_L.baseObject.Custom_Attributes.FL_2_tip.Controller
	global l_f3_base = $Control_Fing_L.baseObject.Custom_Attributes.FL_3_base.Controller
	global l_f3_tip = $Control_Fing_L.baseObject.Custom_Attributes.FL_3_tip.Controller
	
	global l_f4_base = $Control_Fing_L.baseObject.Custom_Attributes.FL_4_base.Controller
	global l_f4_tip = $Control_Fing_L.baseObject.Custom_Attributes.FL_4_tip.Controller
	
	global l_t_base = $Control_Fing_L.baseObject.Custom_Attributes.TL_base.Controller
	global l_t_tip = $Control_Fing_L.baseObject.Custom_Attributes.TL_tip.Controller
	global l_t_roll = $Control_Fing_L.baseObject.Custom_Attributes.TL_roll.Controller
	
	global r_spread = $Control_Fing_R.baseObject.Custom_Attributes.FR_spread.Controller
	global r_f1_base = $Control_Fing_R.baseObject.Custom_Attributes.FR_1_base.Controller
	global r_f1_tip = $Control_Fing_R.baseObject.Custom_Attributes.FR_1_tip.Controller
	global r_f2_base = $Control_Fing_R.baseObject.Custom_Attributes.FR_2_base.Controller
	global r_f2_tip = $Control_Fing_R.baseObject.Custom_Attributes.FR_2_tip.Controller
	global r_f3_base = $Control_Fing_R.baseObject.Custom_Attributes.FR_3_base.Controller
	global r_f3_tip = $Control_Fing_R.baseObject.Custom_Attributes.FR_3_tip.Controller
	
	global r_f4_base = $Control_Fing_R.baseObject.Custom_Attributes.FR_4_base.Controller
	global r_f4_tip = $Control_Fing_R.baseObject.Custom_Attributes.FR_4_tip.Controller
	
	global r_t_base = $Control_Fing_R.baseObject.Custom_Attributes.TR_base.Controller
	global r_t_tip = $Control_Fing_R.baseObject.Custom_Attributes.TR_tip.Controller
	global r_t_roll = $Control_Fing_R.baseObject.Custom_Attributes.TR_roll.Controller
	
	if GetUserProp $Root "Version" != undefined then
	(
		global r_mstr_grip = $Control_Fing_R.baseObject.Custom_Attributes.MasterGrip.Controller
		global l_mstr_grip = $Control_Fing_L.baseObject.Custom_Attributes.MasterGrip.Controller
	)
	global r_elbow = $IK_HandR.baseObject.Custom_Attributes.R_elbow_swivel.Controller
	global l_elbow = $IK_HandL.baseObject.Custom_Attributes.L_elbow_swivel.Controller
	
	global r_arm_ikcon = $IK_HandR.baseObject.Custom_Attributes.ArmR_FK_IK.Controller
	global l_arm_ikcon = $IK_HandL.baseObject.Custom_Attributes.ArmL_FK_IK.Controller
	global r_leg_ikcon = $IK_Control_FootR.baseObject.Custom_Attributes.FK_IK_R.Controller
	global l_leg_ikcon = $IK_Control_FootL.baseObject.Custom_Attributes.FK_IK_L.Controller
	
	global r_deltoid = $Deltoid_R.baseObject.Custom_Attributes.Deltoid_fix_R.Controller
	global l_deltoid = $Deltoid_L.baseObject.Custom_Attributes.Deltoid_fix_L.Controller
	
	--Scaling
	
	global scl_shoulders = $Root.baseObject.Custom_Attributes.Shoulders.Controller
	global scl_mass = $Root.baseObject.Custom_Attributes.Mass.Controller
	global scl_hips = $Root.baseObject.Custom_Attributes.Hips.Controller
	global scl_height = $Root.baseObject.Custom_Attributes.Height.Controller
	global scl_arms = $Root.baseObject.Custom_Attributes.Arms.Controller
	global scl_legs = $Root.baseObject.Custom_Attributes.Legs.Controller
	global scl_head = $Root.baseObject.Custom_Attributes.Head.Controller
	global scl_hands = $Root.baseObject.Custom_Attributes.Hands.Controller
	global scl_feet = $Root.baseObject.Custom_Attributes.Feet.Controller
	global scl_fat = $Root.baseObject.Custom_Attributes.Fat.Controller
	
	-- Squash
	
	global rarm = $IK_HandR.baseObject.Custom_Attributes.RArmStretch.Controller
	global larm = $IK_HandL.baseObject.Custom_Attributes.LArmStretch.Controller
	global rleg = $IK_Control_FootR.baseObject.Custom_Attributes.RLegStretch.Controller
	global lleg = $IK_Control_FootL.baseObject.Custom_Attributes.LLegStretch.Controller
	global torso = $Center_root.baseObject.Custom_Attributes.TorsoStretch.Controller
	global head = $FK_Head.baseObject.Custom_Attributes.HeadStretch.Controller
	
	DestroyDialog crypt_main
	DestroyDialog crypt_fingers
	DestroyDialog cryptIso
	DestroyDialog cryptSquash
	DestroyDialog cryptIDs
	DestroyDialog cryptScaling

	createDialog crypt_main width:404 height:632
	
	registerViewWindow crypt_main
	
	if $IK_Control_FootR.FK_IK_R > 50 then(
	
		IK_R_Leg()
		
	)else(
	
		FK_R_Leg()
	
	)
	if $IK_Control_FootL.FK_IK_L > 50 then(
	
		IK_L_Leg()
		
	)else(
	
		FK_L_Leg()
	
	)
	if $IK_HandR.ArmR_FK_IK > 50 then(
	
		IK_R_Arm()
		
	)else(
	
		FK_R_Arm()
	
	)
	if $IK_HandL.ArmL_FK_IK > 50 then(
	
		IK_L_Arm()
		
	)else(
	
		FK_L_Arm()
	
	)		
	callbacks.addScript #filePostOpen "try Init() catch (callbacks.removeScripts #filePostOpen\nDestroyDialog crypt_main\nDestroyDialog crypt_fingers\nmessageBox \"Control refresh failed. Close and reload script\")" persistant:false

)

function Upgrade = (
	
	SetUserProp $Props_IDs "SkinString" "Corey*EX_Bones"
	
	layer = layermanager.newLayerFromName "Corey"

	layer.addNode $GEO_FootL
	layer.addNode $GEO_FootR
	layer.addNode $GEO_HandL
	layer.addNode $GEO_HandR
	layer.addNode $GEO_Chest
	layer.addNode $GEO_Head
	layer.addNode $GEO_Hips
	
	layerRT = layer.layerAsRefTarg
	
	for o in refs.dependents layerRT do(
	
		try(
			unhide o
			unfreeze o
		)catch()
	
	)
	
	layer = layermanager.newLayerFromName "EX_Bones"

	layer.addNode $Fing_3b_R
	layer.addNode $Fing_1b_R
	layer.addNode $Fing_1c_R
	layer.addNode $Fing_2b_R
	layer.addNode $Fing_4c_R
	layer.addNode $Fing_4b_R
	layer.addNode $Fing_4a_R
	layer.addNode $Fing_3c_R
	layer.addNode $Fing_1a_R
	layer.addNode $Fing_3a_R
	layer.addNode $Fing_2c_R
	layer.addNode $Fing_2a_R
	layer.addNode $T2_R
	layer.addNode $T3_R
	layer.addNode $HandR
	layer.addNode $T1_R
	layer.addNode $Fing_3b_L
	layer.addNode $Fing_1c_L
	layer.addNode $Fing_1b_L
	layer.addNode $Fing_1a_L
	layer.addNode $Fing_2c_L
	layer.addNode $Fing_2b_L
	layer.addNode $Fing_4a_L
	layer.addNode $Fing_3c_L
	layer.addNode $Fing_4b_L
	layer.addNode $Fing_3a_L
	layer.addNode $Fing_4c_L
	layer.addNode $Fing_2a_L
	layer.addNode $HandL
	layer.addNode $T3_L
	layer.addNode $T2_L
	layer.addNode $T1_L
	layer.addNode $Hips
	layer.addNode $LlegR
	layer.addNode $UlegL
	layer.addNode $LlegL
	layer.addNode $UlegR
	layer.addNode $ToeR
	layer.addNode $FootL
	layer.addNode $FootR
	layer.addNode $ToeL
	layer.addNode $Waist
	layer.addNode $UarmR
	layer.addNode $LarmR
	layer.addNode $Head
	layer.addNode $Chest
	layer.addNode $Neck
	layer.addNode $Col_L
	layer.addNode $Col_R
	layer.addNode $Ribs
	layer.addNode $UarmL
	layer.addNode $LarmL
	layer.addNode $Deltoid_L
	layer.addNode $Deltoid_R
	
	layerRT = layer.layerAsRefTarg
	
	for o in refs.dependents layerRT do(
	
		try(
			unhide o
			unfreeze o
		)catch()
	)
	hide #($XFRM_WristR, $TM_R_Elbow, $TM_L_Elbow, $XFRM_WristL, $TM_R_Knee, $TM_L_Knee)

	
)

if $Root != undefined then(
	try(
		
		Init()
		
	)catch(
	
		DestroyDialog crypt_main
		DestroyDialog crypt_fingers
		DestroyDialog cryptIso
		DestroyDialog cryptSquash
		DestroyDialog cryptIDs
		DestroyDialog cryptScaling
	
		messageBox "Synchronization error or rig not loaded. Load rig, or close and restart Max."
	)
)