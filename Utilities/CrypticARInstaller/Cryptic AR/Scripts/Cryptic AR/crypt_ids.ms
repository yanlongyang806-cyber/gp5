rollout cryptIDs "Cryptic ID Manager" width:209 height:362
(
	listBox lbxSkins "Cryptic IDs" pos:[9,6] width:188 height:20
	button btnImport "Import" pos:[10,299] width:90 height:35
	button btnDelete "Delete" pos:[106,299] width:90 height:35
	
	function update_skins = (

		lbxSkins.items = skins
		
		update_stream = stringStream ""
			
		for i = 1 to skins.count do(
	 		
		  format "%*" skins[i] to:update_stream
			
		)
			
		update_string = ""
		
		seek update_stream 0
		
		update_string = readLine update_stream
		
		setUserProp $Props_IDs "SkinString" update_string
		
		crypt_main.skinList.items = skins
		
	)
	
	function importSkin maxfilename = (
	
		temp_layer = layermanager.newLayerFromName "Temp_Layer"
		temp_layer.current = true
		
		temp_layer_ref = temp_layer.layerAsRefTarg
		
		name = ""
		
		if maxfilename == undefined then(
		
			-- Do Nothing
		
		)else(
		
			mergeMAXFile maxfilename #select #promptDups #renameMtlDups
			name = $Name_Template.text
			
			for x in selection do(
			
				temp_layer.addnode x
			
			)
			
			try(
				temp_layer.setname name
			)catch(
			
				-- Possibly add name error checking, or check before
			
			)
			
			--Reparent Components
			
			--TODO: Fix this gross code by making multidimensional array... if possible given weird syntax errors...
			
			for o in $Head_Template.children do (
			
				print o
				try(o.parent = $Head)catch()
			
			)
			for o in $Neck_Template.children do (
			
				try(o.parent = $Neck)catch()
			
			)
			for o in $Chest_Template.children do (
			
				try(o.parent = $Chest)catch()
			
			)
			for o in $Ribs_Template.children do (
			
				try(o.parent = $Ribs)catch()
			
			)
			for o in $Waist_Template.children do (
			
				try(o.parent = $Waist)catch()
			
			)
			for o in $Hips_Template.children do (
			
				try(o.parent = $Hips)catch()
			
			)
			for o in $R_Shoulder_Template.children do (
			
				try(o.parent = $Col_R)catch()
			
			)
			for o in $L_Shoulder_Template.children do (
			
				try(o.parent = $Col_L)catch()
			
			)
			for o in $R_Delt_Template.children do (
			
				try(o.parent = $Deltoid_R)catch()
			
			)
			for o in $L_Delt_Template.children do (
			
				try(o.parent = $Deltoid_L)catch()
			
			)
			for o in $RU_Arm_Template.children do (
			
				try(o.parent = $UarmR)catch()
			
			)
			for o in $LU_Arm_Template.children do (
			
				try(o.parent = $UarmL)catch()
			
			)
			for o in $RL_Arm_Template.children do (
			
				try(o.parent = $LarmR)catch()
			
			)
			for o in $LL_Arm_Template.children do (
			
				try(o.parent = $LarmL)catch()
			
			)
			for o in $R_Elbow_Template.children do (
			
				try(o.parent = $ElbowFan_R)catch()
			
			)
			for o in $L_Elbow_Template.children do (
			
				try(o.parent = $ElbowFan_L)catch()
			
			)
			for o in $R_Hand_Template.children do (
			
				try(o.parent = $HandR)catch()
			
			)
			for o in $L_Hand_Template.children do (
			
				try(o.parent = $HandL)catch()
			
			)
			for o in $RU_Leg_Template.children do (
			
				try(o.parent = $UlegR)catch()
			
			)
			for o in $LU_Leg_Template.children do (
			
				try(o.parent = $UlegL)catch()
			
			)
			for o in $R_Knee_Template.children do (
			
				try(o.parent = $Fan_KneeR)catch()
			
			)
			for o in $L_Knee_Template.children do (
			
				try(o.parent = $Fan_KneeL)catch()
			
			)
			for o in $RL_Leg_Template.children do (
			
				try(o.parent = $LlegR)catch()
			
			)
			for o in $LL_Leg_Template.children do (
			
				try(o.parent = $LlegL)catch()
			
			)
			for o in $R_Foot_Template.children do (
			
				try(o.parent = $FootR) catch()
			
			)
			for o in $L_Foot_Template.children do (
			
				try(o.parent = $FootL)catch()
			
			)
			for o in $R_Toe_Template.children do (
			
				try(o.parent = $ToeR)catch()
			
			)
			for o in $L_Toe_Template.children do (
			
				try(o.parent = $ToeL)catch()
			
			)
			for o in $R_Fing1A_Template.children do (
			
				try(o.parent = $Fing_1c_R)catch()
			
			)
			for o in $R_Fing1B_Template.children do (
			
				try(o.parent = $Fing_1b_R)catch()
			
			)
			for o in $R_Fing1C_Template.children do (
			
				try(o.parent = $Fing_1a_R)catch()
			
			)
			for o in $R_Fing2A_Template.children do (
			
				try(o.parent = $Fing_2c_R)catch()
			
			)
			for o in $R_Fing2B_Template.children do (
			
				try(o.parent = $Fing_2b_R)catch()
			
			)
			for o in $R_Fing2C_Template.children do (
			
				try(o.parent = $Fing_2a_R)catch()
			
			)
			for o in $R_Fing3A_Template.children do (
			
				try(o.parent = $Fing_3c_R)catch()
			
			)
			for o in $R_Fing3B_Template.children do (
			
				try(o.parent = $Fing_3b_R)catch()
			
			)
			for o in $R_Fing3C_Template.children do (
			
				try(o.parent = $Fing_3a_R)catch()
			
			)
			for o in $R_Fing4A_Template.children do (
			
				try(o.parent = $Fing_4c_R)catch()
			
			)
			for o in $R_Fing4B_Template.children do (
			
				try(o.parent = $Fing_4b_R)catch()
			
			)
			for o in $R_Fing4C_Template.children do (
			
				try(o.parent = $Fing_4a_R)catch()
			
			)
			for o in $L_Fing1A_Template.children do (
			
				try(o.parent = $Fing_1c_L)catch()
			
			)
			for o in $L_Fing1B_Template.children do (
			
				try(o.parent = $Fing_1b_L)catch()
			
			)
			for o in $L_Fing1C_Template.children do (
			
				try(o.parent = $Fing_1a_L)catch()
			
			)
			for o in $L_Fing2A_Template.children do (
			
				try(o.parent = $Fing_2c_L)catch()
			
			)
			for o in $L_Fing2B_Template.children do (
			
				try(o.parent = $Fing_2b_L)catch()
			
			)
			for o in $L_Fing2C_Template.children do (
			
				try(o.parent = $Fing_2a_L)catch()
			
			)
			for o in $L_Fing3A_Template.children do (
			
				try(o.parent = $Fing_3c_L)catch()
			
			)
			for o in $L_Fing3B_Template.children do (
			
				try(o.parent = $Fing_3b_L)catch()
			
			)
			for o in $L_Fing3C_Template.children do (
			
				try(o.parent = $Fing_3a_L)catch()
			
			)
			for o in $L_Fing4A_Template.children do (
			
				try(o.parent = $Fing_4c_L)catch()
			
			)
			for o in $L_Fing4B_Template.children do (
			
				try(o.parent = $Fing_4b_L)catch()
			
			)
			for o in $L_Fing4C_Template.children do (
			
				try(o.parent = $Fing_4a_L)catch()
			
			)
			for o in $R_FingTC_Template.children do (
			
				try(o.parent = $T1_R)catch()
			
			)
			for o in $R_FingTB_Template.children do (
			
				try(o.parent = $T2_R)catch()
			
			)
			for o in $R_FingTA_Template.children do (
			
				try(o.parent = $T3_R)catch()
			
			)
			for o in $L_FingTC_Template.children do (
			
				try(o.parent = $T1_L)catch()
			
			)
			for o in $L_FingTB_Template.children do (
			
				try(o.parent = $T2_L)catch()
			
			)
			for o in $L_FingTA_Template.children do (
			
				try(o.parent = $T3_L)catch()
			
			)
							
			
			temp_layer.ishidden = true
			
			
			delete #($Instructions, $Body_Template, $Waist_Template, $Ribs_Template, $RU_Leg_Template, $Hips_Template, $L_Shoulder_Template, $Head_Template, $LL_Arm_Template, $LU_Arm_Template, $Neck_Template, $RU_Arm_Template, $R_Shoulder_Template, $L_Hand_Template, $R_Hand_Template, $RL_Arm_Template, $Chest_Template, $R_Foot_Template, $L_Foot_Template, $LL_Leg_Template, $RL_Leg_Template, $LU_Leg_Template, $L_Toe_Template, $R_Toe_Template, $L_Delt_Template, $R_Delt_Template, $L_Elbow_Template, $R_Elbow_Template, $R_Knee_Template, $L_Knee_Template, $Name_Template, $R_FingTC_Template, $R_Fing1A_Template, $R_Fing1B_Template, $R_Fing2B_Template, $R_Fing4C_Template, $R_Fing2A_Template, $R_Fing3B_Template, $R_Fing1C_Template, $R_Fing3A_Template, $R_Fing4B_Template, $R_Fing4A_Template, $R_Fing2C_Template, $R_Fing3C_Template, $R_FingTB_Template, $R_FingTA_Template, $L_Fing1C_Template, $L_Fing1A_Template, $L_Fing1B_Template, $L_Fing2B_Template, $L_Fing2C_Template, $L_Fing2A_Template, $L_Fing3B_Template, $L_Fing3C_Template, $L_Fing3A_Template, $L_Fing4B_Template, $L_Fing4C_Template, $L_Fing4A_Template, $L_FingTC_Template, $L_FingTB_Template, $L_FingTA_Template)
	
			
			append skins name
					
			--Update Skin Lists
			update_skins()
			
			temp_layer = layermanager.getLayer 0
			temp_layer.current = true
		
		)
	
	)

	
	on cryptIDs open do(
	
		lbxSkins.items = skins
	
	)
	on btnImport pressed do(
	
		temp_filename = getMAXOpenFileName()
		if temp_filename != undefined then importSkin(temp_filename)
	
	)
	on btnDelete pressed do(
	
		temp_name = lbxSkins.selected
		
		if temp_name != "Corey" and temp_name != "EX_Bones" then(
		
			deleteItem skins lbxSkins.selection
			
			temp_layer = layermanager.getLayerFromName temp_name
			temp_layer_RT = temp_layer.layerAsRefTarg
			
			
			for o in refs.dependents temp_layer_RT do (
			
				try(
					delete o
				)catch()
			
			)
			
			layermanager.deleteLayerByName temp_name
			
			update_skins()
		)
			
	
	)

)