rollout cryptIso "Cryptic AR - Isolation" width:248 height:360
(
	checkbox chkIso "Auto-isolate Selection" pos:[10,8] width:129 height:20
	button btnHead "Head" pos:[104,40] width:40 height:32
	button btnTorso "Torso" pos:[96,80] width:56 height:80
	button btnHips "Hips" pos:[96,168] width:56 height:32
	button btnL_FK_Arm "FK" pos:[168,80] width:64 height:24
	button btnL_IK_Arm "IK" pos:[168,112] width:64 height:24
	button btnR_FK_Arm "FK" pos:[16,80] width:64 height:24
	button btnR_IK_Arm "IK" pos:[16,112] width:64 height:24
	button btnR_IK_Leg "IK" pos:[88,224] width:24 height:72
	button btnR_FK_Leg "FK" pos:[56,224] width:24 height:72
	button btnL_FK_Leg "FK" pos:[168,224] width:24 height:72
	button btnL_IK_Leg "IK" pos:[136,224] width:24 height:72
	groupBox grp1 "Left Leg" pos:[128,208] width:72 height:96
	groupBox grp2 "Right Leg" pos:[48,208] width:72 height:96
	groupBox grp3 "Left Arm" pos:[160,64] width:80 height:80
	groupBox grp4 "Right Arm" pos:[8,64] width:80 height:80
	button btnIK "IK" pos:[10,315] width:64 height:35
	button btnFK "FK" pos:[92,315] width:64 height:35
	button btnHybrid "Hybrid" pos:[174,315] width:64 height:35
	
	on btnIK pressed do(
	
		hide #($FK_Head, $FK_Neck, $IK_HandL, $IK_HandR, $IK_Control_FootR, $IK_Control_FootL, $Center_root, $FK_Hips, $FK_Waist, $FK_Ribs, $FK_Chest, $FK_FootR, $FK_Col_R, $FK_UarmR, $FK_LarmR, $FK_HandR, $FK_ToeR, $Deltoid_R, $FK_Col_L, $FK_UarmL, $FK_LarmL, $FK_HandL, $Control_Fing_L, $Deltoid_L, $FK_UlegL, $FK_LlegL, $FK_FootL, $FK_ToeL, $FK_UlegR, $FK_LlegR, $Control_Fing_R); deselect #($FK_Neck, $FK_Head)
		select #($IK_HandL, $IK_HandR, $IK_Control_FootL, $IK_Control_FootR, $Center_root, $FK_Hips, $FK_Waist, $FK_Ribs, $FK_Chest, $FK_Neck, $FK_Col_L, $FK_Col_R, $Control_Fing_R, $Control_Fing_L, $FK_Head)
		
		unhide(selection)
	
	)
	on btnFK pressed do(
	
		hide #($FK_Head, $FK_Neck, $IK_HandL, $IK_HandR, $IK_Control_FootR, $IK_Control_FootL, $Center_root, $FK_Hips, $FK_Waist, $FK_Ribs, $FK_Chest, $FK_FootR, $FK_Col_R, $FK_UarmR, $FK_LarmR, $FK_HandR, $FK_ToeR, $Deltoid_R, $FK_Col_L, $FK_UarmL, $FK_LarmL, $FK_HandL, $Control_Fing_L, $Deltoid_L, $FK_UlegL, $FK_LlegL, $FK_FootL, $FK_ToeL, $FK_UlegR, $FK_LlegR, $Control_Fing_R); deselect #($FK_Neck, $FK_Head)
		select #($FK_Col_L, $FK_Hips, $FK_Waist, $FK_Ribs, $FK_Chest, $FK_Neck, $FK_Head, $FK_Col_R, $FK_FootR, $FK_UarmR, $FK_LarmR, $FK_HandR, $FK_ToeR, $FK_LlegR, $FK_UarmL, $FK_LarmL, $FK_HandL, $FK_UlegL, $FK_LlegL, $FK_FootL, $FK_ToeL, $FK_UlegR, $Control_Fing_R, $Center_root, $Control_Fing_L)
		
		unhide(selection)
	
	)
	on btnHybrid pressed do(
	
		hide #($FK_Head, $FK_Neck, $IK_HandL, $IK_HandR, $IK_Control_FootR, $IK_Control_FootL, $Center_root, $FK_Hips, $FK_Waist, $FK_Ribs, $FK_Chest, $FK_FootR, $FK_Col_R, $FK_UarmR, $FK_LarmR, $FK_HandR, $FK_ToeR, $Deltoid_R, $FK_Col_L, $FK_UarmL, $FK_LarmL, $FK_HandL, $Control_Fing_L, $Deltoid_L, $FK_UlegL, $FK_LlegL, $FK_FootL, $FK_ToeL, $FK_UlegR, $FK_LlegR, $Control_Fing_R); deselect #($FK_Neck, $FK_Head)
		select #($IK_Control_FootL, $IK_Control_FootR, $FK_LarmL, $FK_Hips, $FK_Waist, $FK_Ribs, $FK_Chest, $FK_Neck, $FK_Head, $FK_Col_R, $FK_UarmL, $FK_UarmR, $FK_LarmR, $FK_HandR, $FK_Col_L, $Control_Fing_L, $FK_HandL, $Center_root, $Control_Fing_R)
		
		unhide(selection)
	
	)
	
	on btnHead pressed do (
		
		if keyboard.controlPressed == false then clearselection()
		if(chkIso.checked) do (
		
			hide #($FK_Head, $FK_Neck, $IK_HandL, $IK_HandR, $IK_Control_FootR, $IK_Control_FootL, $Center_root, $FK_Hips, $FK_Waist, $FK_Ribs, $FK_Chest, $FK_FootR, $FK_Col_R, $FK_UarmR, $FK_LarmR, $FK_HandR, $FK_ToeR, $Deltoid_R, $FK_Col_L, $FK_UarmL, $FK_LarmL, $FK_HandL, $Control_Fing_L, $Deltoid_L, $FK_UlegL, $FK_LlegL, $FK_FootL, $FK_ToeL, $FK_UlegR, $FK_LlegR, $Control_Fing_R); deselect #($FK_Neck, $FK_Head)
			
		)
		
		selectMore #($FK_Head, $FK_Neck)
		unhide(selection)
		
	)
	
	on btnTorso pressed do(
	
		if keyboard.controlPressed == false then clearselection()
		if(chkIso.checked) do (
		
			hide #($FK_Head, $FK_Neck, $IK_HandL, $IK_HandR, $IK_Control_FootR, $IK_Control_FootL, $Center_root, $FK_Hips, $FK_Waist, $FK_Ribs, $FK_Chest, $FK_FootR, $FK_Col_R, $FK_UarmR, $FK_LarmR, $FK_HandR, $FK_ToeR, $Deltoid_R, $FK_Col_L, $FK_UarmL, $FK_LarmL, $FK_HandL, $Control_Fing_L, $Deltoid_L, $FK_UlegL, $FK_LlegL, $FK_FootL, $FK_ToeL, $FK_UlegR, $FK_LlegR, $Control_Fing_R); deselect #($FK_Neck, $FK_Head)
			
		)
		
		selectMore #($FK_Ribs, $FK_Chest, $FK_Waist)
		unhide(selection)
	
	)
	
	on btnHips pressed do(
	
		if keyboard.controlPressed == false then clearselection()
		if(chkIso.checked) do (
		
			hide #($FK_Head, $FK_Neck, $IK_HandL, $IK_HandR, $IK_Control_FootR, $IK_Control_FootL, $Center_root, $FK_Hips, $FK_Waist, $FK_Ribs, $FK_Chest, $FK_FootR, $FK_Col_R, $FK_UarmR, $FK_LarmR, $FK_HandR, $FK_ToeR, $Deltoid_R, $FK_Col_L, $FK_UarmL, $FK_LarmL, $FK_HandL, $Control_Fing_L, $Deltoid_L, $FK_UlegL, $FK_LlegL, $FK_FootL, $FK_ToeL, $FK_UlegR, $FK_LlegR, $Control_Fing_R); deselect #($FK_Neck, $FK_Head)
			
		)
		
		selectMore #($FK_Hips, $Center_root)
		unhide(selection)
	
	)
	
	on btnL_FK_Arm pressed do(
	
		if keyboard.controlPressed == false then clearselection()
		if(chkIso.checked) do (
		
			hide #($FK_Head, $FK_Neck, $IK_HandL, $IK_HandR, $IK_Control_FootR, $IK_Control_FootL, $Center_root, $FK_Hips, $FK_Waist, $FK_Ribs, $FK_Chest, $FK_FootR, $FK_Col_R, $FK_UarmR, $FK_LarmR, $FK_HandR, $FK_ToeR, $Deltoid_R, $FK_Col_L, $FK_UarmL, $FK_LarmL, $FK_HandL, $Control_Fing_L, $Deltoid_L, $FK_UlegL, $FK_LlegL, $FK_FootL, $FK_ToeL, $FK_UlegR, $FK_LlegR, $Control_Fing_R); deselect #($FK_Neck, $FK_Head)
			
		)
		
		selectMore #($Control_Fing_L, $FK_UarmL, $FK_LarmL, $FK_HandL, $FK_Col_L)
		unhide(selection)
	
	)
	
	on btnL_IK_Arm pressed do(
	
		if keyboard.controlPressed == false then clearselection()
		if(chkIso.checked) do (
		
			hide #($FK_Head, $FK_Neck, $IK_HandL, $IK_HandR, $IK_Control_FootR, $IK_Control_FootL, $Center_root, $FK_Hips, $FK_Waist, $FK_Ribs, $FK_Chest, $FK_FootR, $FK_Col_R, $FK_UarmR, $FK_LarmR, $FK_HandR, $FK_ToeR, $Deltoid_R, $FK_Col_L, $FK_UarmL, $FK_LarmL, $FK_HandL, $Control_Fing_L, $Deltoid_L, $FK_UlegL, $FK_LlegL, $FK_FootL, $FK_ToeL, $FK_UlegR, $FK_LlegR, $Control_Fing_R); deselect #($FK_Neck, $FK_Head)
			
		)
		
		selectMore #($Control_Fing_L, $FK_Col_L, $IK_HandL)
		unhide(selection)
	
	)
	
	on btnR_FK_Arm pressed do(
	
		if keyboard.controlPressed == false then clearselection()
		if(chkIso.checked) do (
		
			hide #($FK_Head, $FK_Neck, $IK_HandL, $IK_HandR, $IK_Control_FootR, $IK_Control_FootL, $Center_root, $FK_Hips, $FK_Waist, $FK_Ribs, $FK_Chest, $FK_FootR, $FK_Col_R, $FK_UarmR, $FK_LarmR, $FK_HandR, $FK_ToeR, $Deltoid_R, $FK_Col_L, $FK_UarmL, $FK_LarmL, $FK_HandL, $Control_Fing_L, $Deltoid_L, $FK_UlegL, $FK_LlegL, $FK_FootL, $FK_ToeL, $FK_UlegR, $FK_LlegR, $Control_Fing_R); deselect #($FK_Neck, $FK_Head)
			
		)
		
		selectMore #($Control_Fing_R, $FK_UarmR, $FK_LarmR, $FK_HandR, $FK_Col_R)
		unhide(selection)
	
	)
	
	on btnR_IK_Arm pressed do(

		if keyboard.controlPressed == false then clearselection()	
		if(chkIso.checked) do (
		
			hide #($FK_Head, $FK_Neck, $IK_HandL, $IK_HandR, $IK_Control_FootR, $IK_Control_FootL, $Center_root, $FK_Hips, $FK_Waist, $FK_Ribs, $FK_Chest, $FK_FootR, $FK_Col_R, $FK_UarmR, $FK_LarmR, $FK_HandR, $FK_ToeR, $Deltoid_R, $FK_Col_L, $FK_UarmL, $FK_LarmL, $FK_HandL, $Control_Fing_L, $Deltoid_L, $FK_UlegL, $FK_LlegL, $FK_FootL, $FK_ToeL, $FK_UlegR, $FK_LlegR, $Control_Fing_R); deselect #($FK_Neck, $FK_Head)
			
		)
		
		selectMore #($Control_Fing_R, $FK_Col_R, $IK_HandR)
		unhide(selection)
	
	)
	
	on btnL_FK_Leg pressed do(
	
		if keyboard.controlPressed == false then clearselection()
		if(chkIso.checked) do (
		
			hide #($FK_Head, $FK_Neck, $IK_HandL, $IK_HandR, $IK_Control_FootR, $IK_Control_FootL, $Center_root, $FK_Hips, $FK_Waist, $FK_Ribs, $FK_Chest, $FK_FootR, $FK_Col_R, $FK_UarmR, $FK_LarmR, $FK_HandR, $FK_ToeR, $Deltoid_R, $FK_Col_L, $FK_UarmL, $FK_LarmL, $FK_HandL, $Control_Fing_L, $Deltoid_L, $FK_UlegL, $FK_LlegL, $FK_FootL, $FK_ToeL, $FK_UlegR, $FK_LlegR, $Control_Fing_R); deselect #($FK_Neck, $FK_Head)
			
		)
				
		selectMore #($FK_LlegL, $FK_FootL, $FK_ToeL, $FK_UlegL)
		unhide(selection)
		
	)
	
	on btnL_IK_Leg pressed do(
	
		if keyboard.controlPressed == false then clearselection()
		if(chkIso.checked) do (
		
			hide #($FK_Head, $FK_Neck, $IK_HandL, $IK_HandR, $IK_Control_FootR, $IK_Control_FootL, $Center_root, $FK_Hips, $FK_Waist, $FK_Ribs, $FK_Chest, $FK_FootR, $FK_Col_R, $FK_UarmR, $FK_LarmR, $FK_HandR, $FK_ToeR, $Deltoid_R, $FK_Col_L, $FK_UarmL, $FK_LarmL, $FK_HandL, $Control_Fing_L, $Deltoid_L, $FK_UlegL, $FK_LlegL, $FK_FootL, $FK_ToeL, $FK_UlegR, $FK_LlegR, $Control_Fing_R); deselect #($FK_Neck, $FK_Head)
			
		)
		
		selectMore $IK_Control_FootL
		unhide(selection)
		
	)
	
	on btnR_FK_Leg pressed do(
		
		if keyboard.controlPressed == false then clearselection()
		if(chkIso.checked) do (
		
			hide #($FK_Head, $FK_Neck, $IK_HandL, $IK_HandR, $IK_Control_FootR, $IK_Control_FootL, $Center_root, $FK_Hips, $FK_Waist, $FK_Ribs, $FK_Chest, $FK_FootR, $FK_Col_R, $FK_UarmR, $FK_LarmR, $FK_HandR, $FK_ToeR, $Deltoid_R, $FK_Col_L, $FK_UarmL, $FK_LarmL, $FK_HandL, $Control_Fing_L, $Deltoid_L, $FK_UlegL, $FK_LlegL, $FK_FootL, $FK_ToeL, $FK_UlegR, $FK_LlegR, $Control_Fing_R); deselect #($FK_Neck, $FK_Head)
			
		)
		
		selectMore #($FK_LlegR, $FK_FootR, $FK_ToeR, $FK_UlegR)
		unhide(selection)
	
	)
	
	on btnR_IK_Leg pressed do(
	
		if keyboard.controlPressed == false then clearselection()
		if(chkIso.checked) do (
		
			hide #($FK_Head, $FK_Neck, $IK_HandL, $IK_HandR, $IK_Control_FootR, $IK_Control_FootL, $Center_root, $FK_Hips, $FK_Waist, $FK_Ribs, $FK_Chest, $FK_FootR, $FK_Col_R, $FK_UarmR, $FK_LarmR, $FK_HandR, $FK_ToeR, $Deltoid_R, $FK_Col_L, $FK_UarmL, $FK_LarmL, $FK_HandL, $Control_Fing_L, $Deltoid_L, $FK_UlegL, $FK_LlegL, $FK_FootL, $FK_ToeL, $FK_UlegR, $FK_LlegR, $Control_Fing_R); deselect #($FK_Neck, $FK_Head)
			
		)
		
		selectMore $IK_Control_FootR
		unhide(selection)
	
	)
	
)
