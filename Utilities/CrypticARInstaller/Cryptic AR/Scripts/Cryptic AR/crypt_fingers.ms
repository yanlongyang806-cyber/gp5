rollout crypt_fingers "Cryptic AR - Fingers" width:448 height:382
(
	label lbl29 "Master Grip" pos:[16,32] width:61 height:16
	label lbl30 "Master Spread" pos:[16,56] width:72 height:16
	label lbl31 "F1_Base" pos:[16,96] width:48 height:16
	label lbl32 "F2_Base" pos:[16,120] width:48 height:16
	label lbl33 "F3_Base" pos:[16,144] width:48 height:16
	label lbl34 "T_Base" pos:[16,198] width:40 height:16
	label lbl35 "T_Roll" pos:[16,222] width:40 height:16
	label lbl36 "F1_Tip" pos:[112,96] width:40 height:16
	label lbl37 "F2_Tip" pos:[112,120] width:40 height:16
	label lbl38 "F3_Tip" pos:[112,144] width:40 height:16
	label lbl51 "F4_Base" pos:[16,168] width:48 height:16
	label lbl52 "F4_Tip" pos:[112,168] width:40 height:16
	label lbl39 "T_Tip" pos:[112,198] width:40 height:16
	spinner spn_r_f1_base "" pos:[64,96] width:40 height:16 range:[-25,100,0] controller:r_f1_base
	spinner spn_r_f2_base "" pos:[64,120] width:40 height:16 range:[-25,100,0] controller:r_f2_base
	spinner spn_r_f3_base "" pos:[64,144] width:40 height:16 range:[-25,100,0] controller:r_f3_base
	
	spinner spn_r_spread "" pos:[96,56] width:40 height:16 range:[-10,30,0] controller:r_spread
	spinner spn_r_f1_tip "" pos:[152,96] width:40 height:16 range:[-25,100,0] controller:r_f1_tip
	spinner spn_r_f2_tip "" pos:[152,120] width:40 height:16 range:[-25,100,0] controller:r_f2_tip
	spinner spn_r_f3_tip "" pos:[152,144] width:40 height:16 range:[-25,100,0] controller:r_f3_tip
	
	spinner spn_r_t_tip "" pos:[152,198] width:40 height:16 range:[-25,100,0] controller:r_t_tip
	spinner spn_r_t_base "" pos:[64,198] width:40 height:16 range:[-25,100,0] controller:r_t_base
	spinner spn_r_t_roll "" pos:[64,222] width:40 height:16 range:[-25,100,0] controller:r_t_roll
	groupBox grp1 "Right Hand" pos:[8,8] width:192 height:238
	label lbl40 "Master Grip" pos:[256,32] width:61 height:16
	label lbl41 "Master Spread" pos:[256,56] width:72 height:16
	label lbl42 "F1_Base" pos:[256,96] width:48 height:16
	label lbl43 "F2_Base" pos:[256,120] width:48 height:16
	label lbl44 "F3_Base" pos:[256,144] width:48 height:16
	label lbl45 "T_Base" pos:[256,198] width:40 height:16
	label lbl46 "T_Roll" pos:[256,222] width:40 height:16
	label lbl47 "F1_Tip" pos:[352,96] width:40 height:16
	label lbl48 "F2_Tip" pos:[352,120] width:40 height:16
	label lbl49 "F3_Tip" pos:[352,144] width:40 height:16
	label lbl53 "F4_Base" pos:[256,168] width:48 height:16
	label lbl54 "F4_Tip" pos:[352,168] width:40 height:16
	label lbl50 "T_Tip" pos:[352,198] width:40 height:16
	spinner spn_l_f1_base "" pos:[304,96] width:40 height:16 range:[-25,100,0] controller:l_f1_base
	spinner spn_l_f2_base "" pos:[304,120] width:40 height:16 range:[-25,100,0] controller:l_f2_base
	spinner spn_l_f3_base "" pos:[304,144] width:40 height:16 range:[-25,100,0] controller:l_f3_base
	
	spinner spn_l_master "" pos:[336,32] width:40 height:16 range:[-50,100,0]
	spinner spn_r_master "" pos:[96,32] width:40 height:16 range:[-50,100,0]
	
	spinner spn_l_spread "" pos:[336,56] width:40 height:16 range:[-10,30,0] controller:l_spread
	
	spinner spn_l_f1_tip "" pos:[392,96] width:40 height:16 range:[-25,100,0] controller:l_f1_tip
	spinner spn_l_f2_tip "" pos:[392,120] width:40 height:16 range:[-25,100,0] controller:l_f2_tip
	spinner spn_l_f3_tip "" pos:[392,144] width:40 height:16 range:[-25,100,0] controller:l_f3_tip
	
	spinner spn_l_t_tip "" pos:[392,198] width:40 height:16 range:[-25,100,0] controller:l_t_tip
	spinner spn_l_t_base "" pos:[304,198] width:40 height:16 range:[-25,100,0] controller:l_t_base
	spinner spn_l_t_roll "" pos:[304,222] width:40 height:16 range:[-25,100,0] controller:l_t_roll
	groupBox grp2 "Left Hand" pos:[248,8] width:192 height:238
	dropDownList list "Poses List" pos:[128,324] width:192 height:40
	button btn_r_add "Add" pos:[8,286] width:88 height:24
	editText name "" pos:[192,294] width:128 height:17
	button btn_delete "Delete" pos:[128,290] width:56 height:24
	button btn_r_select "Select Node" pos:[8,254] width:88 height:24
	button btn_l_select "Select Node" pos:[352,254] width:88 height:24
	button btn_r_apply "Apply" pos:[8,318] width:88 height:24
	button btn_l_apply "Apply" pos:[352,318] width:88 height:24
	button btn_l_add "Add" pos:[352,286] width:88 height:24
	button btn_r_update "Update Pose" pos:[112,254] width:88 height:24
	button btn_l_update "Update Pose" pos:[248,254] width:88 height:24
	button btn_load "Import" pos:[8,350] width:104 height:24
	button btn_save "Export" pos:[336,350] width:104 height:24
		
	spinner spn_l_f4_tip "" pos:[392,168] width:40 height:16 range:[-25,100,0] controller:l_f4_tip
	spinner spn_l_f4_base "" pos:[304,168] width:40 height:16 range:[-25,100,0] controller:l_f4_base
	spinner spn_r_f4_tip "" pos:[152,168] width:40 height:16 range:[-25,100,0] controller:r_f4_tip
	spinner spn_r_f4_base "" pos:[64,168] width:40 height:16 range:[-25,100,0] controller:r_f4_base
	
	function update_root = (
		
		update_stream = stringStream ""
		
		for i = 1 to num_poses do( 		
		    format "%," poses[i][1] to:update_stream
		    for f = 1 to 12 do(
		 	      format "%," poses[i][2][f] to:update_stream			
			)
			format "%*" poses[i][2][12] to:update_stream
		)
		
		update_string = ""
		
		seek update_stream 0
		
		update_string = readLine update_stream
		
		SetUserProp $Props_Fingers "HandPoses" update_string
			
	)
	
	on crypt_fingers open do(
		
		registerViewWindow crypt_fingers
		
		if (GetUserProp $Props_Fingers "HandPoses" == "") or (GetUserProp $Props_Fingers "HandPoses" == undefined) then SetUserProp $Props_Fingers "HandPoses" "Default,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0*Fist,0.0,85.0,59.0,85.0,59.6,89.5,70.3,85.0,65.0,26.1,64.8,17.3,-8.5"
		
		
		if GetUserProp $Root "Version" != undefined then
		(
			spn_r_master.controller = r_mstr_grip
			spn_l_master.controller = l_mstr_grip
		)
		
		num_poses = 0	
		
		posetmp = #()
		poses = #()
		
		pose_string_tmp = GetUserProp $Props_Fingers "HandPoses"
		pose_string = pose_string_tmp as stringStream
		
		while not eof pose_string do(
		
			append posetmp (readDelimitedString pose_string "*")
			num_poses = num_poses + 1
		
		)
		
		
		for i = 1 to num_poses do(
		
			pose_stream = posetmp[i] as stringStream
			
			append poses #(readDelimitedString pose_stream ",", #(
		
						readValue pose_stream,
						readValue pose_stream,
						readValue pose_stream,
						readValue pose_stream,
						readValue pose_stream,
						readValue pose_stream,
						readValue pose_stream,
						readValue pose_stream,
						readValue pose_stream,
						readValue pose_stream,
						readValue pose_stream,
						readValue pose_stream,
						readValue pose_stream))
					
			)
				
		temp_array = #()
		
		for i = 1 to num_poses do (
		
			append temp_array poses[i][1]
		
		)
		
		list.items = temp_array
		
		name.text = poses[list.selection][1]
		
	)
	
	on btn_r_add pressed  do
	(
		num_poses = num_poses + 1
		
		append poses #(name.text, #(
		
		spn_r_master.value,
		spn_r_f1_base.value,
		spn_r_f1_tip.value,
		spn_r_f2_base.value,
		spn_r_f2_tip.value,
		spn_r_f3_base.value,
		spn_r_f3_tip.value,
		spn_r_f4_base.value,
		spn_r_f4_tip.value,
		spn_r_t_base.value,
		spn_r_t_tip.value,
		spn_r_t_roll.value,
		spn_r_spread.value))
		
		temp_array = #()
		for i = 1 to num_poses do (
		     append temp_array poses[i][1]
		)
		list.items = temp_array
		list.selection = num_poses
		
		update_root()
		
	)
	
	on btn_r_select pressed  do(
		if keyboard.controlPressed == false then clearselection()
		selectMore $Control_Fing_R)
	on btn_l_select pressed  do(
		if keyboard.controlPressed == false then clearselection()
		selectMore $Control_Fing_L)
	
	on btn_l_apply pressed  do
	(
		
		spn_r_master.value = poses[list.selection][2][1]
		spn_l_f1_base.value = poses[list.selection][2][2]
		spn_l_f1_tip.value = poses[list.selection][2][3]
		spn_l_f2_base.value = poses[list.selection][2][4]
		spn_l_f2_tip.value = poses[list.selection][2][5]
		spn_l_f3_base.value = poses[list.selection][2][6]
		spn_l_f3_tip.value = poses[list.selection][2][7]
		spn_l_f4_base.value = poses[list.selection][2][8]
		spn_l_f4_tip.value = poses[list.selection][2][9]
		spn_l_t_base.value = poses[list.selection][2][10]
		spn_l_t_tip.value = poses[list.selection][2][11]
		spn_l_t_roll.value = poses[list.selection][2][12]
		spn_l_spread.value = poses[list.selection][2][13]
		
	)
	on btn_l_add pressed  do
	(

		num_poses = num_poses + 1
		
		append poses #(name.text, #(
		spn_r_master.value,
		spn_l_f1_base.value,
		spn_l_f1_tip.value,
		spn_l_f2_base.value,
		spn_l_f2_tip.value,
		spn_l_f3_base.value,
		spn_l_f3_tip.value,
		spn_l_f4_base.value,
		spn_l_f4_tip.value,
		spn_l_t_base.value,
		spn_l_t_tip.value,
		spn_l_t_roll.value,
		spn_l_spread.value))
		
		temp_array = #()
		for i = 1 to num_poses do (
		     append temp_array poses[i][1]
		)
		list.items = temp_array
		list.selection = num_poses

		update_root()
		
	)
	on btn_r_update pressed  do
	(
	
		poses[list.selection] = #(name.text, #(
		spn_r_master.value,
		spn_r_f1_base.value,
		spn_r_f1_tip.value,
		spn_r_f2_base.value,
		spn_r_f2_tip.value,
		spn_r_f3_base.value,
		spn_r_f3_tip.value,
		spn_r_f4_base.value,
		spn_r_f4_tip.value,
		spn_r_t_base.value,
		spn_r_t_tip.value,
		spn_r_t_roll.value,
		spn_r_spread.value))
		
		temp_array = #()
		for i = 1 to num_poses do (
		     append temp_array poses[i][1]
		)
		list.items = temp_array
	
		update_root()
		
	)
	on btn_l_update pressed  do
(
	
	poses[list.selection] = #(name.text, #(
		spn_r_master.value,
		spn_l_f1_base.value,
		spn_l_f1_tip.value,
		spn_l_f2_base.value,
		spn_l_f2_tip.value,
		spn_l_f3_base.value,
		spn_l_f3_tip.value,
		spn_l_f4_base.value,
		spn_l_f4_tip.value,
		spn_l_t_base.value,
		spn_l_t_tip.value,
		spn_l_t_roll.value,
		spn_l_spread.value))
		
		temp_array = #()
		for i = 1 to num_poses do (
		     append temp_array poses[i][1]
		)
		list.items = temp_array
	
		update_root()
	
	)	
		
	on btn_save pressed  do
	(
	
			file_name = getSaveFileName caption:"Select Export Destination:" filename:"hand.pos"
		
		try
		(	
			out_file = createfile file_name
			
			format "%\n" num_poses to:out_file
			for i = 1 to num_poses do( 		
			    format "%," poses[i][1] to:out_file
			    for f = 1 to 12 do(
			 	      format "%," poses[i][2][f] to:out_file			
				)
				format "%\n" poses[i][2][12] to:out_file
			)
			close out_file
		)catch(
			--Do Nothing
		)
	)
	on list selected sel do
	(	
		name.text = poses[list.selection][1]
		
	)
	on name entered text do pose_name = name.text
	
	on btn_load pressed  do
	(
		file_name = getOpenFileName caption:"Select File to Import:" filename:"hand.pos"
		
		num_poses = 0	
		
		poses = #()
		
		try (
			in_file = openFile file_name
			
			num_poses = readValue in_file
			
			for i = 1 to num_poses do(
			
				append poses #(readDelimitedString in_file ",", #(
			
							readValue in_file,
							readValue in_file,
							readValue in_file,
							readValue in_file,
							readValue in_file,
							readValue in_file,
							readValue in_file,
							readValue in_file,
							readValue in_file,
							readValue in_file,
							readValue in_file,
							readValue in_file,
							readValue in_file))
						
				)
				
			close in_file
			
			temp_array = #()
			
			for i = 1 to num_poses do (
			
				append temp_array poses[i][1]
			
			)
			
			list.items = temp_array
			
			name.text = poses[list.selection][1]
			
			update_root()
			
		)catch(
		--Do nothing

		)

	)
	on btn_r_apply pressed do(
	
		spn_r_master.value = poses[list.selection][2][1]
		spn_r_f1_base.value = poses[list.selection][2][2]
		spn_r_f1_tip.value = poses[list.selection][2][3]
		spn_r_f2_base.value = poses[list.selection][2][4]
		spn_r_f2_tip.value = poses[list.selection][2][5]
		spn_r_f3_base.value = poses[list.selection][2][6]
		spn_r_f3_tip.value = poses[list.selection][2][7]
		spn_r_f4_base.value = poses[list.selection][2][8]
		spn_r_f4_tip.value = poses[list.selection][2][9]
		spn_r_t_base.value = poses[list.selection][2][10]
		spn_r_t_tip.value = poses[list.selection][2][11]
		spn_r_t_roll.value = poses[list.selection][2][12]
		spn_r_spread.value = poses[list.selection][2][13]
		
	)
	
	on btn_delete pressed do(
	
		if num_poses > 1 then (
		
			temp_array = deleteItem poses list.selection
			poses = temp_array
			num_poses = num_poses - 1
			
			temp_array = #()
		
			for i = 1 to num_poses do (
			
				append temp_array poses[i][1]
			
			)
			
			list.items = temp_array
			
			list.selection = 1
			
			name.text = poses[list.selection][1]

		)
		
		update_root()
	
	)
	
)
