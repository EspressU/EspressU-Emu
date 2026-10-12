#include "Cafe/OS/libs/h264_avc/parser/H264Parser.h"

void parse_hrd_parameters(h264ParserState_t* h264ParserState, RBSPInputBitstream& nalStream)
{
	uint32 cpb_cnt_minus1 = nalStream.readUV_E();
	uint8 bit_rate_scale = nalStream.readBits<4>();
	uint8 cpb_size_scale = nalStream.readBits<4>();
	for (uint8 schedSelIdx = 0; schedSelIdx <= cpb_cnt_minus1; schedSelIdx++)
	{
		uint32 bit_rate_value_minus1 = nalStream.readUV_E();
		uint32 cpb_size_value_minus1 = nalStream.readUV_E();
		uint8 cbr_flag = nalStream.readBit();
	}
	uint8 initial_cpb_removal_delay_length_minus1 = nalStream.readBits<5>();
	uint8 cpb_removal_delay_length_minus1 = nalStream.readBits<5>();
	uint8 dpb_output_delay_length_minus1 = nalStream.readBits<5>();
	uint8 time_offset_length = nalStream.readBits<5>();
}

void parseNAL_scaling_list4x4(RBSPInputBitstream& rbspStream, h264_scaling_matrix4x4_t& scalingMatrix4x4)
{
	if (rbspStream.readBit() == 0)
	{
		scalingMatrix4x4.isPresent = 0;
		return;
	}
	scalingMatrix4x4.isPresent = 1;

	cemu_assert_debug(false);
	sint32 lastScale = 8;
	sint32 nextScale = 8;
	for (sint32 j = 0; j < 4 * 4; j++)
	{
		if (nextScale != 0)
		{
			sint32 delta_scale = rbspStream.readSV_E();
			nextScale = (lastScale + delta_scale + 256) % 256;
			scalingMatrix4x4.UseDefaultScalingMatrix = (j == 0 && nextScale == 0);
		}
		scalingMatrix4x4.list[j] = (nextScale == 0) ? lastScale : nextScale;
		lastScale = scalingMatrix4x4.list[j];
	}
}

void parseNAL_scaling_list8x8(RBSPInputBitstream& rbspStream, h264_scaling_matrix8x8_t& scalingMatrix8x8)
{
	if (rbspStream.readBit() == 0)
	{
		scalingMatrix8x8.isPresent = 0;
		return;
	}
	scalingMatrix8x8.isPresent = 1;

	cemu_assert_debug(false);
	sint32 lastScale = 8;
	sint32 nextScale = 8;
	for (sint32 j = 0; j < 8 * 8; j++)
	{
		if (nextScale != 0)
		{
			sint32 delta_scale = rbspStream.readSV_E();
			nextScale = (lastScale + delta_scale + 256) % 256;
			scalingMatrix8x8.UseDefaultScalingMatrix = (j == 0 && nextScale == 0);
		}
		scalingMatrix8x8.list[j] = (nextScale == 0) ? lastScale : nextScale;
		lastScale = scalingMatrix8x8.list[j];
	}
}

void parseNAL_pps_scaling_lists(h264ParserState_t* h264ParserState, RBSPInputBitstream& nalStream)
{
	parseNAL_scaling_list4x4(nalStream, h264ParserState->pps.ScalingList4x4[0]);
	parseNAL_scaling_list4x4(nalStream, h264ParserState->pps.ScalingList4x4[1]);
	parseNAL_scaling_list4x4(nalStream, h264ParserState->pps.ScalingList4x4[2]);
	parseNAL_scaling_list4x4(nalStream, h264ParserState->pps.ScalingList4x4[3]);
	parseNAL_scaling_list4x4(nalStream, h264ParserState->pps.ScalingList4x4[4]);
	parseNAL_scaling_list4x4(nalStream, h264ParserState->pps.ScalingList4x4[5]);

	if (h264ParserState->pps.transform_8x8_mode_flag)
	{
		parseNAL_scaling_list8x8(nalStream, h264ParserState->pps.ScalingList8x8[0]);
		parseNAL_scaling_list8x8(nalStream, h264ParserState->pps.ScalingList8x8[1]);

		if (h264ParserState->sps.chroma_format_idc == 3)
			cemu_assert(false);
	}
}

void parseNAL_sps_scaling_lists(h264ParserState_t* h264ParserState, RBSPInputBitstream& nalStream)
{
	parseNAL_scaling_list4x4(nalStream, h264ParserState->sps.ScalingList4x4[0]);
	parseNAL_scaling_list4x4(nalStream, h264ParserState->sps.ScalingList4x4[1]);
	parseNAL_scaling_list4x4(nalStream, h264ParserState->sps.ScalingList4x4[2]);
	parseNAL_scaling_list4x4(nalStream, h264ParserState->sps.ScalingList4x4[3]);
	parseNAL_scaling_list4x4(nalStream, h264ParserState->sps.ScalingList4x4[4]);
	parseNAL_scaling_list4x4(nalStream, h264ParserState->sps.ScalingList4x4[5]);

	parseNAL_scaling_list8x8(nalStream, h264ParserState->sps.ScalingList8x8[0]);
	parseNAL_scaling_list8x8(nalStream, h264ParserState->sps.ScalingList8x8[1]);

	if (h264ParserState->sps.chroma_format_idc == 3)
		cemu_assert(false);
}

bool parseNAL_seq_parameter_set_rbsp(h264ParserState_t* h264ParserState, h264ParserOutput_t* output, RBSPInputBitstream& nalStream)
{
	memset(&h264ParserState->sps, 0, sizeof(h264State_seq_parameter_set_t));

	h264ParserState->sps.profile_idc = nalStream.readU8();
	h264ParserState->sps.constraint = nalStream.readU8();
	h264ParserState->sps.level_idc = nalStream.readU8();

	h264ParserState->sps.separate_colour_plane_flag = 0;
	h264ParserState->sps.chroma_format_idc = 1;
	h264ParserState->sps.qpprime_y_zero_transform_bypass_flag = 0;
	h264ParserState->sps.seq_scaling_matrix_present_flag = 0;

	uint32 seq_parameter_set_id = nalStream.readUV_E();
	if (h264ParserState->sps.profile_idc == 100 || h264ParserState->sps.profile_idc == 110 || h264ParserState->sps.profile_idc == 122 ||
		h264ParserState->sps.profile_idc == 244 || h264ParserState->sps.profile_idc == 44 || h264ParserState->sps.profile_idc == 83 ||
		h264ParserState->sps.profile_idc == 86 || h264ParserState->sps.profile_idc == 118 || h264ParserState->sps.profile_idc == 128 ||
		h264ParserState->sps.profile_idc == 138 || h264ParserState->sps.profile_idc == 139 || h264ParserState->sps.profile_idc == 134 || h264ParserState->sps.profile_idc == 135)
	{
		h264ParserState->sps.chroma_format_idc = nalStream.readUV_E();
		if (h264ParserState->sps.chroma_format_idc == 3)
		{
			h264ParserState->sps.separate_colour_plane_flag = nalStream.readBit();
		}
		h264ParserState->sps.bit_depth_luma_minus8 = nalStream.readUV_E();
		h264ParserState->sps.bit_depth_chroma_minus8 = nalStream.readUV_E();
		h264ParserState->sps.qpprime_y_zero_transform_bypass_flag = nalStream.readBit();
		h264ParserState->sps.seq_scaling_matrix_present_flag = nalStream.readBit();
		if (h264ParserState->sps.seq_scaling_matrix_present_flag)
		{
			parseNAL_sps_scaling_lists(h264ParserState, nalStream);
		}
	}

	h264ParserState->sps.log2_max_frame_num_minus4 = nalStream.readUV_E();
	h264ParserState->sps.pic_order_cnt_type = nalStream.readUV_E();
	if (h264ParserState->sps.pic_order_cnt_type == 0)
	{
		h264ParserState->sps.log2_max_pic_order_cnt_lsb_minus4 = nalStream.readUV_E();
	}
	else if (h264ParserState->sps.pic_order_cnt_type == 2)
	{
	}
	else
	{
		cemu_assert_debug(false);
	}
	h264ParserState->sps.num_ref_frames = nalStream.readUV_E();
	h264ParserState->sps.gaps_in_frame_num_value_allowed_flag = nalStream.readBit();
	h264ParserState->sps.pic_width_in_mbs_minus1 = nalStream.readUV_E();
	h264ParserState->sps.pic_height_in_map_units_minus1 = nalStream.readUV_E();
	h264ParserState->sps.frame_mbs_only_flag = nalStream.readBit();
	if (h264ParserState->sps.frame_mbs_only_flag == 0)
	{
		h264ParserState->sps.mb_adaptive_frame_field_flag = nalStream.readBit();
		cemu_assert_debug(false);
	}
	else
		h264ParserState->sps.mb_adaptive_frame_field_flag = 0;

	h264ParserState->sps.direct_8x8_inference_flag = nalStream.readBit();
	if (h264ParserState->sps.frame_mbs_only_flag == 0 && h264ParserState->sps.direct_8x8_inference_flag != 1)
	{
		cemu_assert_debug(false);
	}

	h264ParserState->sps.frame_cropping_flag = nalStream.readBit();
	if (h264ParserState->sps.frame_cropping_flag)
	{
		h264ParserState->sps.frame_crop_left_offset = nalStream.readUV_E();
		h264ParserState->sps.frame_crop_right_offset = nalStream.readUV_E();
		h264ParserState->sps.frame_crop_top_offset = nalStream.readUV_E();
		h264ParserState->sps.frame_crop_bottom_offset = nalStream.readUV_E();
	}
	uint8 vui_parameters_present_flag = nalStream.readBit();
	if (vui_parameters_present_flag)
	{
		uint8 aspect_ratio_info_present_flag = nalStream.readBit();
		if (aspect_ratio_info_present_flag)
		{
			uint32 aspect_ratio_idc = nalStream.readBits<8>();
			if (aspect_ratio_idc == 255)
			{
				uint16 sar_width = nalStream.readBits<16>();
				uint16 sar_height = nalStream.readBits<16>();
			}
		}
		uint8 overscan_info_present_flag = nalStream.readBit();
		if (overscan_info_present_flag)
		{
			cemu_assert_debug(false);
		}
		uint8 video_signal_type_present_flag = nalStream.readBit();
		if (video_signal_type_present_flag)
		{
			uint8 video_format = nalStream.readBits<3>();
			uint8 video_full_range_flag = nalStream.readBit();
			uint8 colour_description_present_flag = nalStream.readBit();
			if (colour_description_present_flag)
			{
				uint8 colour_primaries = nalStream.readBits<8>();
				uint8 transfer_characteristics = nalStream.readBits<8>();
				uint8 matrix_coefficients = nalStream.readBits<8>();
			}
		}
		uint8 chroma_loc_info_present_flag = nalStream.readBit();
		if (chroma_loc_info_present_flag)
		{
			uint32 chroma_sample_loc_type_top_field = nalStream.readUV_E();
			uint32 chroma_sample_loc_type_bottom_field = nalStream.readUV_E();
		}
		uint8 timing_info_present_flag = nalStream.readBit();
		if (timing_info_present_flag)
		{
			uint32 num_units_in_tick = nalStream.readBits<32>();
			uint32 time_scale = nalStream.readBits<32>();
			uint8 fixed_frame_rate_flag = nalStream.readBits<1>();
		}
		uint8 nal_hrd_parameters_present_flag = nalStream.readBit();
		if (nal_hrd_parameters_present_flag)
		{
			parse_hrd_parameters(h264ParserState, nalStream);
		}
		uint8 vcl_hrd_parameters_present_flag = nalStream.readBit();
		if (vcl_hrd_parameters_present_flag)
		{
			parse_hrd_parameters(h264ParserState, nalStream);
		}
		if (nal_hrd_parameters_present_flag || vcl_hrd_parameters_present_flag)
		{
			uint8 low_delay_hrd_flag = nalStream.readBit();
		}
		uint8 pic_struct_present_flag = nalStream.readBit();
		uint8 bitstream_restriction_flag = nalStream.readBit();
		if (bitstream_restriction_flag)
		{
			uint8 motion_vectors_over_pic_boundaries_flag = nalStream.readBit();
			uint32 max_bytes_per_pic_denom = nalStream.readUV_E();
			uint32 max_bits_per_mb_denom = nalStream.readUV_E();
			uint32 log2_max_mv_length_horizontal = nalStream.readUV_E();
			uint32 log2_max_mv_length_vertical = nalStream.readUV_E();
			uint32 max_num_reorder_frames = nalStream.readUV_E();
			uint32 max_dec_frame_buffering = nalStream.readUV_E();
		}
	}
	bool nalValid = true;
	if (nalStream.readTrailingRBSPBits() == false)
		nalValid = false;
	if (nalValid)
	{
		if(output)
			output->hasSPS = true;
		h264ParserState->hasSPS = true;
	}
	return true;
}

bool parseNAL_pic_parameter_set_rbsp(h264ParserState_t* h264ParserState, h264ParserOutput_t* output, RBSPInputBitstream& nalStream)
{
	memset(&h264ParserState->pps, 0, sizeof(h264State_pic_parameter_set_t));

	h264ParserState->pps.pic_parameter_set_id = nalStream.readUV_E();
	h264ParserState->pps.seq_parameter_set_id = nalStream.readUV_E();
	h264ParserState->pps.entropy_coding_mode_flag = nalStream.readBit();
	h264ParserState->pps.bottom_field_pic_order_in_frame_present_flag = nalStream.readBit();

	h264ParserState->pps.num_slice_groups_minus1 = nalStream.readUV_E();
	if (h264ParserState->pps.num_slice_groups_minus1 > 0)
	{
		cemu_assert_debug(false);
		return false;
	}

	h264ParserState->pps.num_ref_idx_l0_default_active_minus1 = nalStream.readUV_E();
	h264ParserState->pps.num_ref_idx_l1_default_active_minus1 = nalStream.readUV_E();
	h264ParserState->pps.weighted_pred_flag = nalStream.readBit();
	h264ParserState->pps.weighted_bipred_idc = nalStream.readBits<2>();
	h264ParserState->pps.pic_init_qp_minus26 = nalStream.readSV_E();
	h264ParserState->pps.pic_init_qs_minus26 = nalStream.readSV_E();
	h264ParserState->pps.chroma_qp_index_offset = nalStream.readSV_E();

	h264ParserState->pps.deblocking_filter_control_present_flag = nalStream.readBit();
	h264ParserState->pps.constrained_intra_pred_flag = nalStream.readBit();
	h264ParserState->pps.redundant_pic_cnt_present_flag = nalStream.readBit();

	if (nalStream.more_rbsp_data())
	{
		h264ParserState->pps.transform_8x8_mode_flag = nalStream.readBit();
		h264ParserState->pps.pic_scaling_matrix_present_flag = nalStream.readBit();
		if (h264ParserState->pps.pic_scaling_matrix_present_flag)
		{
			parseNAL_pps_scaling_lists(h264ParserState, nalStream);
		}
		h264ParserState->pps.second_chroma_qp_index_offset = nalStream.readSV_E();
	}

	bool nalValid = true;
	if (nalStream.readTrailingRBSPBits() == false)
		nalValid = false;
	if (nalValid)
	{
		if(output)
			output->hasPPS = true;
		h264ParserState->hasPPS = true;
	}
	return true;
}

bool h264Parser_ParseSPS(uint8* data, uint32 length, h264State_seq_parameter_set_t& sps)
{
	h264ParserState_t parserState;
	RBSPInputBitstream nalStream(data, length);
	bool r = parseNAL_seq_parameter_set_rbsp(&parserState, nullptr, nalStream);
	if(!r || !parserState.hasSPS)
		return false;
	sps = parserState.sps;
	return true;
}

void parseNAL_ref_pic_list_modification(const h264State_seq_parameter_set_t& sps, const h264State_pic_parameter_set_t& pps, RBSPInputBitstream& nalStream, nal_slice_header_t* sliceHeader)
{
	if (!sliceHeader->slice_type.isSliceTypeI() && !sliceHeader->slice_type.isSliceTypeSI())
	{
		uint8 ref_pic_list_modification_flag_l0 = nalStream.readBit();
		if (ref_pic_list_modification_flag_l0)
		{
			sliceHeader->pic_list_modification0Count = 0;
			uint32 modType;
			while(true)
			{
				if (sliceHeader->pic_list_modification0Count >= 32)
				{
					cemu_assert_debug(false);
					return;
				}
				modType = nalStream.readUV_E();
				sliceHeader->pic_list_modification0Array[sliceHeader->pic_list_modification0Count].type = modType;
				if (modType == 0 || modType == 1)
				{
					sliceHeader->pic_list_modification0Array[sliceHeader->pic_list_modification0Count].abs_diff_pic_num_minus1 = nalStream.readUV_E();
				}
				else if (modType == 2)
				{
					sliceHeader->pic_list_modification0Array[sliceHeader->pic_list_modification0Count].long_term_pic_num = nalStream.readUV_E();
				}
				else if (modType == 3)
				{
					break;
				}
				else
				{
					cemu_assert_debug(false);
					return;
				}
				sliceHeader->pic_list_modification0Count++;
			}
		}
	}
	if (sliceHeader->slice_type.isSliceTypeB())
	{
		uint8 ref_pic_list_modification_flag_l1 = nalStream.readBit();
		if (ref_pic_list_modification_flag_l1)
		{
			cemu_assert_debug(false);
			while (true)
			{
				if (sliceHeader->pic_list_modification1Count >= 32)
				{
					cemu_assert_debug(false);
					return;
				}
				uint32 modType = nalStream.readUV_E();
				sliceHeader->pic_list_modification1Array[sliceHeader->pic_list_modification1Count].type = modType;
				if (modType == 0 || modType == 1)
				{
					sliceHeader->pic_list_modification1Array[sliceHeader->pic_list_modification1Count].abs_diff_pic_num_minus1 = nalStream.readUV_E();
				}
				else if (modType == 2)
				{
					sliceHeader->pic_list_modification1Array[sliceHeader->pic_list_modification1Count].long_term_pic_num = nalStream.readUV_E();
				}
				else if(modType == 3)
				{
					break;
				}
				else
				{
					cemu_assert_debug(false);
					break;
				}
				sliceHeader->pic_list_modification0Count++;
			}
			if (sliceHeader->pic_list_modification1Count > 0)
			{
				cemuLog_logDebug(LogType::Force, "sliceHeader->pic_list_modification1Count non-zero is not supported");
				cemu_assert_unimplemented();
			}
		}
	}
}

void parseNAL_dec_ref_pic_marking(const h264State_seq_parameter_set_t& sps, const h264State_pic_parameter_set_t& pps, RBSPInputBitstream& nalStream, nal_slice_header_t* sliceHeader)
{
	sliceHeader->memory_management_control_operation_num = 0;
	if (sliceHeader->IdrPicFlag)
	{
		uint8 no_output_of_prior_pics_flag = nalStream.readBit();
		if (no_output_of_prior_pics_flag)
			cemu_assert_debug(false);
		uint8 long_term_reference_flag = nalStream.readBit();
		if (long_term_reference_flag)
			cemu_assert_debug(false);

		sliceHeader->adaptive_ref_pic_marking_mode_flag = 1;
	}
	else
	{
		sliceHeader->adaptive_ref_pic_marking_mode_flag = nalStream.readBit();
		if (sliceHeader->adaptive_ref_pic_marking_mode_flag)
		{
			while (true)
			{
				uint32 memory_management_control_operation = nalStream.readUV_E();
				if (memory_management_control_operation == nal_slice_header_t::MEMOP_END)
					break;
				if (sliceHeader->memory_management_control_operation_num >= 16)
				{
					cemu_assert_debug(false);
					return;
				}
				sliceHeader->memory_management_control_operation[sliceHeader->memory_management_control_operation_num].op = memory_management_control_operation;
				if (memory_management_control_operation == nal_slice_header_t::MEMOP_REMOVE_REF_FROM_SHORT_TERM || memory_management_control_operation == nal_slice_header_t::MEMOP_MAKE_LONG_TERM_REF)
				{
					sliceHeader->memory_management_control_operation[sliceHeader->memory_management_control_operation_num].difference_of_pic_nums_minus1 = nalStream.readUV_E();
				}
				else if (memory_management_control_operation == nal_slice_header_t::MEMOP_REMOVE_REF_FROM_LONG_TERM)
				{
					sliceHeader->memory_management_control_operation[sliceHeader->memory_management_control_operation_num].long_term_pic_num = nalStream.readUV_E();
				}
				if (memory_management_control_operation == nal_slice_header_t::MEMOP_MAKE_LONG_TERM_REF || memory_management_control_operation == nal_slice_header_t::MEMOP_MAKE_CURRENT_LONG_TERM_REF)
				{
					sliceHeader->memory_management_control_operation[sliceHeader->memory_management_control_operation_num].long_term_frame_idx = nalStream.readUV_E();
				}
				if (memory_management_control_operation == nal_slice_header_t::MEMOP_MAX_LONG_TERM_INDEX)
				{
					sliceHeader->memory_management_control_operation[sliceHeader->memory_management_control_operation_num].max_long_term_frame_idx_plus1 = nalStream.readUV_E();
				}
				sliceHeader->memory_management_control_operation_num++;
			}
		}
	}
}

void parseNAL_pred_weight_table(const h264State_seq_parameter_set_t& sps, const h264State_pic_parameter_set_t& pps, RBSPInputBitstream& nalStream, nal_slice_header_t* sliceHeader)
{
	uint8 luma_log2_weight_denom = nalStream.readUV_E();

	uint32 ChromaArrayType;
	if (sps.separate_colour_plane_flag == 0)
		ChromaArrayType = sps.chroma_format_idc;
	else
		ChromaArrayType = 0;

	if (ChromaArrayType != 0)
	{
		uint32 chroma_log2_weight_denom = nalStream.readUV_E();
	}

	for (uint32 i = 0; i <= sliceHeader->num_ref_idx_l0_active_minus1; i++)
	{
		uint8 luma_weight_l0_flag = nalStream.readBit();
		if (luma_weight_l0_flag)
		{
			uint32 luma_weight_l0 = nalStream.readSV_E();
			uint32 luma_offset_l0 = nalStream.readSV_E();
		}
		if (ChromaArrayType != 0)
		{
			uint8 chroma_weight_l0_flag = nalStream.readBit();
			if (chroma_weight_l0_flag)
			{
				for (sint32 j = 0; j < 2; j++)
				{
					uint32 chroma_weight_l0 = nalStream.readSV_E();
					uint32 chroma_offset_l0 = nalStream.readSV_E();
				}
			}
		}
	}
	if (sliceHeader->slice_type.isSliceTypeB())
	{
		for (uint32 i = 0; i <= sliceHeader->num_ref_idx_l1_active_minus1; i++)
		{
			uint8 luma_weight_l1_flag = nalStream.readBit();
			if (luma_weight_l1_flag)
			{
				cemu_assert_debug(false);
			}
			if (ChromaArrayType != 0)
			{
				cemu_assert_debug(false);
			}
		}
	}
}

void parseNAL_slice_header(const h264State_seq_parameter_set_t& sps, const h264State_pic_parameter_set_t& pps, RBSPInputBitstream& nalStream, uint8 nal_unit_type, uint8 nal_ref_idc, nal_slice_header_t* sliceHeader)
{
	bool IdrPicFlag = nal_unit_type == 5;

	memset(sliceHeader, 0, sizeof(nal_slice_header_t));
	sliceHeader->nal_ref_idc = nal_ref_idc;
	sliceHeader->nal_unit_type = nal_unit_type;
	sliceHeader->first_mb_in_slice = nalStream.readUV_E();
	sliceHeader->slice_type = { nalStream.readUV_E() };
	sliceHeader->pic_parameter_set_id = nalStream.readUV_E();

	if (sps.separate_colour_plane_flag == 1)
	{
		cemu_assert_debug(false);
		return;
	}

	sliceHeader->frame_num = nalStream.readBits(sps.log2_max_frame_num_minus4 + 4);

	if (sps.frame_mbs_only_flag == 0)
	{
		sliceHeader->field_pic_flag = nalStream.readBit();
		if (sliceHeader->field_pic_flag)
		{
			sliceHeader->bottom_field_flag = nalStream.readBit();
		}
	}

	sliceHeader->IdrPicFlag = IdrPicFlag?1:0;

	if (IdrPicFlag)
	{
		sliceHeader->idr_pic_id = nalStream.readUV_E();
	}
	if (sps.pic_order_cnt_type == 0)
	{
		sliceHeader->pic_order_cnt_lsb = nalStream.readBits(sps.log2_max_pic_order_cnt_lsb_minus4 + 4);
		if (pps.bottom_field_pic_order_in_frame_present_flag && sliceHeader->field_pic_flag == 0)
			sliceHeader->delta_pic_order_cnt_bottom = nalStream.readSV_E();
	}
	else if (sps.pic_order_cnt_type == 1)
	{
		cemu_assert(false);
	}
	if (pps.redundant_pic_cnt_present_flag)
	{
		sliceHeader->redundant_pic_cnt = nalStream.readUV_E();
	}

	if (sliceHeader->slice_type.isSliceTypeB())
	{
		sliceHeader->direct_spatial_mv_pred_flag = nalStream.readBit();
	}

	sliceHeader->num_ref_idx_l0_active_minus1 = pps.num_ref_idx_l0_default_active_minus1;
	sliceHeader->num_ref_idx_l1_active_minus1 = pps.num_ref_idx_l1_default_active_minus1;

	if (sliceHeader->slice_type.isSliceTypeP() || sliceHeader->slice_type.isSliceTypeSP() || sliceHeader->slice_type.isSliceTypeB())
	{
		sliceHeader->num_ref_idx_active_override_flag = nalStream.readBit();
		if (sliceHeader->num_ref_idx_active_override_flag)
		{
			sliceHeader->num_ref_idx_l0_active_minus1 = nalStream.readUV_E();
			if (sliceHeader->slice_type.isSliceTypeB())
			{
				sliceHeader->num_ref_idx_l1_active_minus1 = nalStream.readUV_E();
			}
		}
	}

	if (nal_unit_type == 20 || nal_unit_type == 21)
	{
		cemu_assert_debug(false);
	}
	else
	{
		parseNAL_ref_pic_list_modification(sps, pps, nalStream, sliceHeader);
	}

	if ((pps.weighted_pred_flag && (sliceHeader->slice_type.isSliceTypeP() || sliceHeader->slice_type.isSliceTypeSP())) || (pps.weighted_bipred_idc == 1 && sliceHeader->slice_type.isSliceTypeB()))
	{
		parseNAL_pred_weight_table(sps, pps, nalStream, sliceHeader);
	}

	if (sliceHeader->nal_ref_idc != 0)
	{
		parseNAL_dec_ref_pic_marking(sps, pps, nalStream, sliceHeader);
	}

	if (pps.entropy_coding_mode_flag && !sliceHeader->slice_type.isSliceTypeI() && !sliceHeader->slice_type.isSliceTypeSI())
	{
		uint32 cabac_init_idc = nalStream.readUV_E();
		cemu_assert_debug(cabac_init_idc <= 2);
	}

	sint32 slice_qp_delta = nalStream.readSV_E();
	if (sliceHeader->slice_type.isSliceTypeSP() || sliceHeader->slice_type.isSliceTypeSI())
	{
		if (sliceHeader->slice_type.isSliceTypeSP())
		{
			uint8 sp_for_switch_flag = nalStream.readBit();
		}
		sint32 slice_qs_delta = nalStream.readSV_E();
	}

	if (pps.deblocking_filter_control_present_flag)
	{
		uint32 disable_deblocking_filter_idc = nalStream.readUV_E();
		if (disable_deblocking_filter_idc != 1)
		{
			sint32 slice_alpha_c0_offset_div2 = nalStream.readSV_E();
			sint32 slice_beta_offset_div2 = nalStream.readSV_E();
		}
	}
	if (pps.num_slice_groups_minus1 > 0 && pps.slice_group_map_type >= 3 && pps.slice_group_map_type <= 5)
	{
		cemu_assert_debug(false);
	}
}

void _calculateFrameOrder(h264ParserState_t* h264ParserState, const h264State_seq_parameter_set_t& sps, const h264State_pic_parameter_set_t& pps, nal_slice_header_t* sliceHeader)
{
	if (sps.pic_order_cnt_type == 0)
	{
		uint32 prevPicOrderCntMsb;
		uint32 prevPicOrderCntLsb;
		if (sliceHeader->IdrPicFlag)
		{
			sliceHeader->calculated.TopFieldOrderCnt = 0;

			prevPicOrderCntMsb = 0;
			prevPicOrderCntLsb = 0;

		}
		else
		{
			uint32 prevRefPic_PicOrderCntMsb = h264ParserState->picture_order.prevPicOrderCntMsb;
			uint32 prevRefPic_pic_order_cnt_lsb = h264ParserState->picture_order.prevPicOrderCntLsb;

			prevPicOrderCntMsb = prevRefPic_PicOrderCntMsb;
			prevPicOrderCntLsb = prevRefPic_pic_order_cnt_lsb;

		}

		uint32 MaxPicOrderCntLsb = 1 << (sps.log2_max_pic_order_cnt_lsb_minus4 + 4);

		uint32 PicOrderCntMsb;

		if ((sliceHeader->pic_order_cnt_lsb < prevPicOrderCntLsb) && (((sint32)prevPicOrderCntLsb - (sint32)sliceHeader->pic_order_cnt_lsb) >= (sint32)(MaxPicOrderCntLsb / 2)))
			PicOrderCntMsb = prevPicOrderCntMsb + MaxPicOrderCntLsb;
		else if ((sliceHeader->pic_order_cnt_lsb > prevPicOrderCntLsb) && (((sint32)sliceHeader->pic_order_cnt_lsb - (sint32)prevPicOrderCntLsb) > (sint32)(MaxPicOrderCntLsb / 2)))
			PicOrderCntMsb = prevPicOrderCntMsb - MaxPicOrderCntLsb;
		else
			PicOrderCntMsb = prevPicOrderCntMsb;

		uint32 TopFieldOrderCnt = PicOrderCntMsb + sliceHeader->pic_order_cnt_lsb;

		if (sliceHeader->IdrPicFlag != 0 && TopFieldOrderCnt != 0)
			cemu_assert(false);

		sliceHeader->calculated.TopFieldOrderCnt = TopFieldOrderCnt;

		h264ParserState->picture_order.prevPicOrderCntMsb = PicOrderCntMsb;
		h264ParserState->picture_order.prevPicOrderCntLsb = sliceHeader->pic_order_cnt_lsb;
	}
	else if (sps.pic_order_cnt_type == 2)
	{
		uint32 prevFrameNum = h264ParserState->picture_order.prevFrameNum;

		uint32 FrameNumOffset;
		if (sliceHeader->IdrPicFlag)
		{
			FrameNumOffset = 0;
		}
		else
		{
			uint32 prevFrameNumOffset = h264ParserState->picture_order.prevFrameNumOffset;

			if (prevFrameNum > sliceHeader->frame_num)
				FrameNumOffset = prevFrameNumOffset + sps.getMaxFrameNum();
			else
				FrameNumOffset = prevFrameNumOffset;
		}

		uint32 tempPicOrderCnt;
		if (sliceHeader->IdrPicFlag == 1)
			tempPicOrderCnt = 0;
		else if (sliceHeader->nal_ref_idc == 0)
			tempPicOrderCnt = 2 * (FrameNumOffset + sliceHeader->frame_num) - 1;
		else
			tempPicOrderCnt = 2 * (FrameNumOffset + sliceHeader->frame_num);

		uint32 TopFieldOrderCnt = 0;
		uint32 BottomFieldOrderCnt = 0;

		if (sliceHeader->field_pic_flag == 0)
		{
			TopFieldOrderCnt = tempPicOrderCnt;
			BottomFieldOrderCnt = tempPicOrderCnt;
		}
		else if (sliceHeader->bottom_field_flag != 0)
		{
			cemu_assert_debug(false);
			BottomFieldOrderCnt = tempPicOrderCnt;
		}
		else
		{
			cemu_assert_debug(false);
			TopFieldOrderCnt = tempPicOrderCnt;
		}

		sliceHeader->calculated.TopFieldOrderCnt = TopFieldOrderCnt;

		h264ParserState->picture_order.prevFrameNum = sliceHeader->frame_num;
		h264ParserState->picture_order.prevFrameNumOffset = FrameNumOffset;
	}
	else
	{
		cemu_assert_debug(false);
	}
}

void parseNAL_slice_layer_without_partitioning_rbsp(h264ParserState_t* h264ParserState, h264ParserOutput_t* output, sint32 streamSubOffset, sint32 streamSubLength, RBSPInputBitstream& nalStream, uint8 nal_ref_idc, uint8 nal_unit_type)
{
	nal_slice_t slice = {};
	cemu_assert_debug(h264ParserState->hasPPS && h264ParserState->hasSPS);
	parseNAL_slice_header(h264ParserState->sps, h264ParserState->pps, nalStream, nal_unit_type, nal_ref_idc, &slice.slice_header);
	_calculateFrameOrder(h264ParserState, h264ParserState->sps, h264ParserState->pps, &slice.slice_header);
	if (output->sliceCount >= output->sliceInfo.size())
	{
		cemu_assert_debug(false);
		return;
	}
	output->sliceInfo[output->sliceCount].header = slice.slice_header;
	output->sliceInfo[output->sliceCount].streamSubOffset = streamSubOffset;
	output->sliceInfo[output->sliceCount].streamSubSize = streamSubLength;
	output->sliceCount++;
}

void h264Parse(h264ParserState_t* h264ParserState, h264ParserOutput_t* output, uint8* data, uint32 length, bool parseSlices)
{
	memset(output, 0, sizeof(h264ParserOutput_t));
	NALInputBitstream nalStream(data, length);

	if (nalStream.hasError())
	{
		cemu_assert_debug(false);
		return;
	}

	while (true)
	{
		if (nalStream.isEndOfStream())
			break;
		RBSPInputBitstream rbspStream;
		if (nalStream.getNextRBSP(rbspStream) == false)
			break;

		sint32 streamSubOffset = (sint32)(rbspStream.getBasePtr() - data);
		sint32 streamSubLength = rbspStream.getBaseLength();

		uint8 nalHeaderByte = rbspStream.readU8();
		if ((nalHeaderByte & 0x80) != 0)
		{
			cemu_assert_debug(false);
			continue;
		}
		uint8 nal_ref_idc = (nalHeaderByte >> 5) & 0x3;
		uint8 nal_unit_type = (nalHeaderByte >> 0) & 0x1f;

		if (nal_unit_type == 14 || nal_unit_type == 20 || nal_unit_type == 21)
		{
			cemu_assert_debug(false);
		}
		switch (nal_unit_type)
		{
		case 1:
			if (parseSlices)
				parseNAL_slice_layer_without_partitioning_rbsp(h264ParserState, output, streamSubOffset, streamSubLength, rbspStream, nal_ref_idc, nal_unit_type);
			break;
		case 5:
			if (parseSlices)
				parseNAL_slice_layer_without_partitioning_rbsp(h264ParserState, output, streamSubOffset, streamSubLength, rbspStream, nal_ref_idc, nal_unit_type);
			break;
		case 6:
			break;
		case 7:
			cemu_assert_debug(parseNAL_seq_parameter_set_rbsp(h264ParserState, output, rbspStream));
			break;
		case 8:
			cemu_assert_debug(parseNAL_pic_parameter_set_rbsp(h264ParserState, output, rbspStream));
			break;
		case 9:
			break;
		case 10:
			break;
		case 12:
			break;
		default:
			cemuLog_logDebug(LogType::Force, "Unsupported NAL unit type {}", nal_unit_type);
			cemu_assert_debug(false);
			break;
		}
	}
}

sint32 h264GetUnitLength(h264ParserState_t* h264ParserState, uint8* data, uint32 length)
{
	NALInputBitstream nalStream(data, length);

	if (nalStream.hasError())
	{
		cemu_assert_debug(false);
		return -1;
	}

	sint32 startCodeOffset = 0;
	bool hasStartcode = false;
	while (startCodeOffset < (sint32)(length - 3))
	{
		if (data[startCodeOffset + 0] == 0x00 && data[startCodeOffset + 1] == 0x00 && data[startCodeOffset + 2] == 0x01)
		{
			hasStartcode = true;
			break;
		}
		startCodeOffset++;
	}
	if (hasStartcode == false)
		return -1;
	data += startCodeOffset;
	length -= startCodeOffset;

	while (true)
	{
		if (nalStream.isEndOfStream())
			break;
		RBSPInputBitstream rbspStream;
		if (nalStream.getNextRBSP(rbspStream, true) == false)
			break;

		sint32 streamSubOffset = (sint32)(rbspStream.getBasePtr() - data);
		sint32 streamSubLength = rbspStream.getBaseLength();

		uint8 nalHeaderByte = rbspStream.readU8();
		if ((nalHeaderByte & 0x80) != 0)
		{
			cemu_assert_debug(false);
			continue;
		}
		uint8 nal_ref_idc = (nalHeaderByte >> 5) & 0x3;
		uint8 nal_unit_type = (nalHeaderByte >> 0) & 0x1f;
		if (nal_unit_type == 14 || nal_unit_type == 20 || nal_unit_type == 21)
		{
			cemu_assert_debug(false);
			continue;
		}
		switch (nal_unit_type)
		{
		case 1:
		case 5:
		{
			return (sint32)((rbspStream.getBasePtr() + rbspStream.getBaseLength()) - data) + startCodeOffset;

			break;
		}
		case 6:
			break;
		case 7:
			cemu_assert_debug(parseNAL_seq_parameter_set_rbsp(h264ParserState, nullptr, rbspStream));
			break;
		case 8:
			cemu_assert_debug(parseNAL_pic_parameter_set_rbsp(h264ParserState, nullptr, rbspStream));
			break;
		case 9:
			break;
		case 10:
			break;
		default:
			cemuLog_logDebug(LogType::Force, "Unsupported NAL unit type {}", nal_unit_type);
			assert_dbg();
			break;
		}
	}
	return -1;
}

static const unsigned char h264_default_4x4_Intra[16] =
{
	6, 13, 13, 20,
	20, 20, 28, 28,
	28, 28, 32, 32,
	32, 37, 37, 42
};

static const unsigned char h264_default_4x4_Inter[16] =
{
	10, 14, 14, 20,
	20, 20, 24, 24,
	24, 24, 27, 27,
	27, 30, 30, 34
};

static const unsigned char h264_default_8x8_Intra[64] =
{
	6, 10, 10, 13, 11, 13, 16, 16,
	16, 16, 18, 18, 18, 18, 18, 23,
	23, 23, 23, 23, 23, 25, 25, 25,
	25, 25, 25, 25, 27, 27, 27, 27,
	27, 27, 27, 27, 29, 29, 29, 29,
	29, 29, 29, 31, 31, 31, 31, 31,
	31, 33, 33, 33, 33, 33, 36, 36,
	36, 36, 38, 38, 38, 40, 40, 42
};

static const unsigned char h264_default_8x8_Inter[64] =
{
	9, 13, 13, 15, 13, 15, 17, 17,
	17, 17, 19, 19, 19, 19, 19, 21,
	21, 21, 21, 21, 21, 22, 22, 22,
	22, 22, 22, 22, 24, 24, 24, 24,
	24, 24, 24, 24, 25, 25, 25, 25,
	25, 25, 25, 27, 27, 27, 27, 27,
	27, 28, 28, 28, 28, 28, 30, 30,
	30, 30, 32, 32, 32, 33, 33, 35
};

void h264Parser_getScalingMatrix4x4(h264State_seq_parameter_set_t* sps, h264State_pic_parameter_set_t* pps, nal_slice_header_t* sliceHeader, sint32 index, uint8* matrix4x4)
{
	if (pps->pic_scaling_matrix_present_flag)
	{
		if (pps->ScalingList4x4[index].isPresent)
		{
			cemu_assert(false);
			memcpy(matrix4x4, pps->ScalingList4x4[index].list, 4 * 4 * sizeof(uint8));
		}
		else
		{
			if (index < 3)
				memcpy(matrix4x4, h264_default_4x4_Intra, 4 * 4 * sizeof(uint8));
			else
				memcpy(matrix4x4, h264_default_4x4_Inter, 4 * 4 * sizeof(uint8));
		}
		return;
	}
	if (sps->seq_scaling_matrix_present_flag)
	{
		if (sps->ScalingList4x4[index].isPresent)
		{
			cemu_assert(false);
			memcpy(matrix4x4, sps->ScalingList4x4[index].list, 4 * 4 * sizeof(uint8));
		}
		else
		{
			if (index < 3)
				memcpy(matrix4x4, h264_default_4x4_Intra, 4 * 4 * sizeof(uint8));
			else
				memcpy(matrix4x4, h264_default_4x4_Inter, 4 * 4 * sizeof(uint8));
		}
		return;
	}
	memset(matrix4x4, 16, 4 * 4 * sizeof(uint8));
}

void h264Parser_getScalingMatrix8x8(h264State_seq_parameter_set_t* sps, h264State_pic_parameter_set_t* pps, nal_slice_header_t* sliceHeader, sint32 index, uint8* matrix8x8)
{
	if (pps->pic_scaling_matrix_present_flag)
	{
		if (pps->ScalingList8x8[index].isPresent)
		{
			cemu_assert(false);
			memcpy(matrix8x8, pps->ScalingList8x8[index].list, 8 * 8 * sizeof(uint8));
		}
		else
		{
			if ((index&1) == 0)
				memcpy(matrix8x8, h264_default_8x8_Intra, 8 * 8 * sizeof(uint8));
			else
				memcpy(matrix8x8, h264_default_8x8_Inter, 8 * 8 * sizeof(uint8));
		}
		return;
	}
	if (sps->seq_scaling_matrix_present_flag)
	{
		if (sps->ScalingList8x8[index].isPresent)
		{
			cemu_assert(false);
			memcpy(matrix8x8, sps->ScalingList8x8[index].list, 8 * 8 * sizeof(uint8));
		}
		else
		{
			if ((index & 1) == 0)
				memcpy(matrix8x8, h264_default_8x8_Intra, 8 * 8 * sizeof(uint8));
			else
				memcpy(matrix8x8, h264_default_8x8_Inter, 8 * 8 * sizeof(uint8));
		}
		return;
	}
	memset(matrix8x8, 16, 8 * 8 * sizeof(uint8));
}
