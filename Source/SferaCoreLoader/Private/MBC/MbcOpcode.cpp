#include "MBC/MbcOpcode.h"
#include <iomanip>
#include <sstream>

static std::string H(uint32 value)
{
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << value;
    return out.str();
}
static std::string S(int32 value) { return std::to_string(value); }
static std::string U(uint32 value) { return std::to_string(value); }
static std::vector<FMbcOpcodeSpec> MakeOpcodes()
{
    return
    {
        {
            0x00,'\0',0x55F7D8,0x479430,"sub_479430","unknown_opcode_trap",EMbcOperandFormat::Trap,"dispatcher trap; handler decrements PC and reports unknown script code"
        },
        {
            0x21,'!',0x55F6E0,0x4799D0,"sub_4799D0","logical_not",EMbcOperandFormat::None,"unary logical not"
        },
        {
            0x22,'\"',0x55F798,0x4776C0,"sub_4776C0","to_int_prev",EMbcOperandFormat::None,"converts previous float value to integer"
        },
        {
            0x25,'%',0x55F678,0x476F20,"sub_476F20","mod",EMbcOperandFormat::None,"integer modulo"
        },
        {
            0x26,'&',0x55F758,0x476DD0,"sub_476DD0","address_of",EMbcOperandFormat::None,"turns stack metadata into pointer/slice descriptor"
        },
        {
            0x28,'(',0x55F7D0,0x476860,"sub_476860","push_imm_u16",EMbcOperandFormat::TypedImmU16,"push typed immediate: type:u8, value:u16"
        },
        {
            0x29,')',0x55F7D8,0x4768C0,"sub_4768C0","push_imm_i8",EMbcOperandFormat::TypedImmI8,"push typed immediate: type:u8, value:i8"
        },
        {
            0x2A,'*',0x55F668,0x476EC0,"sub_476EC0","mul",EMbcOperandFormat::None,"binary numeric multiplication"
        },
        {
            0x2B,'+',0x55F658,0x476E20,"sub_476E20","add",EMbcOperandFormat::None,"binary numeric addition"
        },
        {
            0x2C,',',0x55F600,0x4767C0,"sub_4767C0","set_arg_count",EMbcOperandFormat::U8,"sets VM argument count dword_86232C"
        },
        {
            0x2D,'-',0x55F660,0x476E70,"sub_476E70","sub",EMbcOperandFormat::None,"binary numeric subtraction"
        },
        {
            0x2E,'.',0x55F768,0x477560,"sub_477560","to_float",EMbcOperandFormat::None,"converts current integer slot to float"
        },
        {
            0x2F,'/',0x55F670,0x4795F0,"sub_4795F0","div",EMbcOperandFormat::None,"binary numeric division"
        },
        {
            0x30,'0',0x55F608,0x4767E0,"sub_4767E0","stack_frame_reset",EMbcOperandFormat::None,"main-loop special: reset stack pointer to current frame base"
        },
        {
            0x31,'1',0x55F680,0x479470,"sub_479470","push_stack_frame",EMbcOperandFormat::None,"pushes current stack base on VM stack-of-stacks"
        },
        {
            0x32,'2',0x55F688,0x4794B0,"sub_4794B0","pop_stack_frame",EMbcOperandFormat::None,"pops VM stack-of-stacks"
        },
        {
            0x39,'9',0x55F618,0x476800,"sub_476800","push_imm32",EMbcOperandFormat::TypedImm32,"push typed immediate: type:u8, value:u32"
        },
        {
            0x3A,':',0x55F770,0x477580,"sub_477580","to_float_prev",EMbcOperandFormat::None,"converts previous stack value to float"
        },
        {
            0x3B,';',0x55F7A8,0x4777F0,"sub_4777F0","force_two_ints",EMbcOperandFormat::None,"marks two stack cells as integer/bool type"
        },
        {
            0x3C,'<',0x55F6A8,0x4770F0,"sub_4770F0","lt",EMbcOperandFormat::None,"binary less-than comparison"
        },
        {
            0x3D,'=',0x55F650,0x476C20,"sub_476C20","store",EMbcOperandFormat::None,"stores top value into lvalue below it"
        },
        {
            0x3E,'>',0x55F6A0,0x477070,"sub_477070","gt",EMbcOperandFormat::None,"binary greater-than comparison"
        },
        {
            0x41,'A',0x55F748,0x476920,"sub_476920","push_inline_span",EMbcOperandFormat::InlineSpan,"pushes inline span/slice: data_offset:u32, length:u16"
        },
        {
            0x43,'C',0x55F638,0x476B90,"sub_476B90","program_activate",EMbcOperandFormat::ProgramI16,"activates target program"
        },
        {
            0x47,'G',0x55F5F8,0x4767A0,"sub_4767A0","jmp_rel32",EMbcOperandFormat::Rel32,"unconditional relative jump; target is pc_after_opcode + rel32"
        },
        {
            0x48,'H',0x55F760,0x477550,"sub_477550","halt_interpreter",EMbcOperandFormat::None,"sets interpreter stop flag"
        },
        {
            0x49,'I',0x55F610,0x47AF30,"sub_47AF30","jfalse_rel32",EMbcOperandFormat::JFalseRel32,"pop/test condition; false jumps by rel32, true skips operand"
        },
        {
            0x4A,'J',0x55F7B0,0x4767B0,"sub_4767B0","jmp_rel16",EMbcOperandFormat::Rel16,"unconditional relative short jump; target is pc_after_opcode + rel16"
        },
        {
            0x4B,'K',0x55F7B8,0x47AF50,"sub_47AF50","jfalse_rel16",EMbcOperandFormat::JFalseRel16,"pop/test condition; false jumps by rel16, true skips operand"
        },
        {
            0x4C,'L',0x55F7F8,0x477270,"sub_477270","logical_or_rel16",EMbcOperandFormat::LogicalOrRel16,"short-circuit OR"
        },
        {
            0x4D,'M',0x55F800,0x4772C0,"sub_4772C0","logical_and_rel16",EMbcOperandFormat::LogicalAndRel16,"short-circuit AND"
        },
        {
            0x4F,'O',0x55F6C8,0x4796D0,"sub_4796D0","program_prologue",EMbcOperandFormat::Prologue,"binds call parameters into local data slots"
        },
        {
            0x50,'P',0x55F750,0x476B60,"sub_476B60","program_stop",EMbcOperandFormat::ProgramI16,"sets target program inactive"
        },
        {
            0x52,'R',0x55F628,0x476A70,"sub_476A70","program_restart",EMbcOperandFormat::ProgramI16,"sets target program PC to primary entry"
        },
        {
            0x53,'S',0x55F630,0x476B30,"sub_476B30","program_reset_alt_pc",EMbcOperandFormat::ProgramI16,"sets target program PC to alternate saved entry"
        },
        {
            0x55,'U',0x55F6F8,0x476AD0,"sub_476AD0","program_restart_child",EMbcOperandFormat::ProgramI16,"sets target program PC and records parent"
        },
        {
            0x5B,'[',0x55F780,0x4775F0,"sub_4775F0","ptr_add_scaled_u16",EMbcOperandFormat::U16,"pops index/pointer pair and adds immediate-scaled offset"
        },
        {
            0x5D,']',0x55F788,0x477640,"sub_477640","ptr_sub_scaled_u16",EMbcOperandFormat::U16,"pops index/pointer pair and subtracts immediate-scaled offset"
        },
        {
            0x5E,'^',0x55F750,0x476CD0,"sub_476CD0","deref",EMbcOperandFormat::None,"dereferences pointer/slice descriptor on stack"
        },
        {
            0x60,'`',0x55F790,0x477690,"sub_477690","to_int",EMbcOperandFormat::None,"converts current float slot to integer"
        },
        {
            0x61,'a',0x55F620,0x47AF90,"sub_47AF90","array_index_abs",EMbcOperandFormat::ArrayAbs,"indexed absolute array reference"
        },
        {
            0x62,'b',0x55F730,0x47B120,"sub_47B120","array2_index_checked",EMbcOperandFormat::Array2Checked,"indexed relative array/slice reference with explicit count"
        },
        {
            0x63,'c',0x55F640,0x47B680,"sub_47B680","call_rel32",EMbcOperandFormat::CallRel32,"pushes return address then jumps by rel32"
        },
        {
            0x64,'d',0x55F7E0,0x47B410,"sub_47B410","slice_offset_ref",EMbcOperandFormat::SliceOffsetRef,"relative slice reference"
        },
        {
            0x65,'e',0x55F7D8,0x476990,"sub_476990","push_typed_span_ref",EMbcOperandFormat::TypedSpanRef,"push typed span reference"
        },
        {
            0x66,'f',0x55F700,0x477500,"sub_477500","builtin_call",EMbcOperandFormat::Builtin,"second-level dispatcher jpt_47754E"
        },
        {
            0x67,'g',0x55F740,0x479460,"sub_479460","import_stub_u32",EMbcOperandFormat::ImportStubU32,"5-byte import/unlinked-call stub; patched by linker"
        },
        {
            0x68,'h',0x55F7E8,0x47B590,"sub_47B590","slice_offset_span",EMbcOperandFormat::SliceOffsetSpan,"relative slice/span reference"
        },
        {
            0x69,'i',0x55F5F0,0x4766E0,"sub_4766E0","push_data_ref",EMbcOperandFormat::DataRef,"push typed lvalue/value from data section"
        },
        {
            0x6C,'l',0x55F7F0,0x476A00,"sub_476A00","push_inline_typed_span",EMbcOperandFormat::TypedSpanInline,"push typed inline span"
        },
        {
            0x6D,'m',0x55F738,0x47B2C0,"sub_47B2C0","array2_index",EMbcOperandFormat::Array2,"indexed relative array/slice reference"
        },
        {
            0x72,'r',0x55F648,0x4794D0,"sub_4794D0","return",EMbcOperandFormat::None,"returns through VM return stack"
        },
        {
            0x74,'t',0x55F7C0,0x476BD0,"sub_476BD0","return_local",EMbcOperandFormat::None,"returns to local return stack entry or terminates program"
        },
        {
            0x7E,'~',0x55F778,0x4775A0,"sub_4775A0","swap",EMbcOperandFormat::None,"swaps top two VM stack cells"
        },
        {
            0xC9,0xC9,0x55F7E0,0x47B810,"sub_47B810","call_linked_function",EMbcOperandFormat::None,"runtime-linked function call by pending name"
        },
        {
            0xCF,0xCF,0x55F7A0,0x4776F0,"sub_4776F0","ptr_add_assign_u16",EMbcOperandFormat::U16,"adds immediate u16 to pointer/value"
        },
        {
            0xD3,0xD3,0x55F7A8,0x477730,"sub_477730","ptr_sub_assign_u16",EMbcOperandFormat::U16,"subtracts immediate u16 from pointer/value"
        },
        {
            0xD6,0xD6,0x55F720,0x477770,"sub_477770","add_assign_u16",EMbcOperandFormat::U16,"adds immediate u16 to integer lvalue"
        },
        {
            0xD7,0xD7,0x55F728,0x4777B0,"sub_4777B0","sub_assign_u16",EMbcOperandFormat::U16,"subtracts immediate u16 from integer lvalue"
        },
        {
            0xE1,0xE1,0x55F6B0,0x477170,"sub_477170","ge",EMbcOperandFormat::None,"binary greater-or-equal comparison"
        },
        {
            0xE8,0xE8,0x55F6D8,0x477310,"sub_477310","force_int_type_alt",EMbcOperandFormat::None,"marks current stack slot as integer/bool type"
        },
        {
            0xEB,0xEB,0x55F6D0,0x477310,"sub_477310","force_int_type",EMbcOperandFormat::None,"marks current stack slot as integer/bool type"
        },
        {
            0xEC,0xEC,0x55F6B8,0x4771F0,"sub_4771F0","le",EMbcOperandFormat::None,"binary less-or-equal comparison"
        },
        {
            0xED,0xED,0x55F698,0x476FF0,"sub_476FF0","ne",EMbcOperandFormat::None,"binary inequality comparison"
        },
        {
            0xEF,0xEF,0x55F6F0,0x477340,"sub_477340","pre_inc",EMbcOperandFormat::None,"pre-increment lvalue"
        },
        {
            0xF0,0xF0,0x55F690,0x476F70,"sub_476F70","eq",EMbcOperandFormat::None,"binary equality comparison"
        },
        {
            0xF1,0xF1,0x55F6C0,0x479660,"sub_479660","neg",EMbcOperandFormat::None,"unary numeric negation"
        },
        {
            0xF3,0xF3,0x55F6F8,0x4773B0,"sub_4773B0","pre_dec",EMbcOperandFormat::None,"pre-decrement lvalue"
        },
        {
            0xF6,0xF6,0x55F708,0x477420,"sub_477420","post_inc",EMbcOperandFormat::None,"post-increment lvalue"
        },
        {
            0xF7,0xF7,0x55F710,0x477490,"sub_477490","post_dec",EMbcOperandFormat::None,"post-decrement lvalue"
        }
    };
}
static std::vector<FMbcBuiltinSpec> MakeBuiltins()
{
    return
    {
        {
            0,0x55FB28,0x47B930,"loc_47B930","debug_print_float","debug UI fatal-print of a float argument","exact"
        },
        {
            1,0x55FB2C,0x47B930,"loc_47B930","debug_print_float_alias","alias of debug_print_float","exact"
        },
        {
            2,0x55FB30,0x47B980,"loc_47B980","print_string_or_exit","prints string argument; with zero args exits interpreter process","exact"
        },
        {
            3,0x55FB34,0x47B9C0,"loc_47B9C0","sin","pushes sin(float_arg)","exact"
        },
        {
            4,0x55FB38,0x47B9F0,"loc_47B9F0","cos","pushes cos(float_arg)","exact"
        },
        {
            5,0x55FB3C,0x47BAF0,"loc_47BAF0","abs_float","pushes absolute value of float_arg","exact"
        },
        {
            6,0x55FB40,0x47BA90,"loc_47BA90","abs_int","pushes absolute value of int_arg","exact"
        },
        {
            7,0x55FB44,0x47BA50,"loc_47BA50","atan2","pushes atan2(y,x)","exact"
        },
        {
            8,0x55FB48,0x47BB20,"loc_47BB20","push_vm_tick","pushes VM loop tick counter","recovered"
        },
        {
            9,0x55FB4C,0x47F5B0,"loc_47F5B0","sscanf","sscanf wrapper","exact"
        },
        {
            10,0x55FB50,0x480490,"loc_480490","window_api","nested window/UI dispatcher","partial"
        },
        {
            11,0x55FB54,0x47BDA0,"loc_47BDA0","pack_rgb24","packs three integer components","recovered"
        },
        {
            12,0x55FB58,0x47BF10,"loc_47BF10","sqrt_abs_float","pushes sqrt(abs(float_arg))","exact"
        },
        {
            13,0x55FB5C,0x47BF50,"loc_47BF50","push_runtime_handle","pushes runtime/global handle by selector","exact"
        },
        {
            14,0x55FB60,0x47BFA0,"loc_47BFA0","push_runtime_flag_byte","pushes runtime flag byte","exact"
        },
        {
            15,0x55FB64,0x481D20,"loc_481D20","alloc_span","allocates VM memory span","recovered"
        },
        {
            16,0x55FB68,0x47BFD0,"loc_47BFD0","ffprc_load","loads/starts a process by script name","recovered"
        },
        {
            17,0x55FB6C,0x47C030,"loc_47C030","ffprc_unload","unloads/stops process","recovered"
        },
        {
            18,0x55FB70,0x47C0C0,"loc_47C0C0","ffprc_link","links/resolves a process/script name","recovered"
        },
        {
            19,0x55FB74,0x47C150,"loc_47C150","ffprc_state","pushes process state","recovered"
        },
        {
            20,0x55FB78,0x47C570,"loc_47C570","send_to_process_id","validates destination process id and sends","recovered"
        },
        {
            21,0x55FB7C,0x47C5C0,"loc_47C5C0","send_to_process_zero","sends to process zero","recovered"
        },
        {
            22,0x55FB80,0x47C5E0,"loc_47C5E0","last_process_result","pushes last process/native result","recovered"
        },
        {
            23,0x55FB84,0x47C640,"loc_47C640","arg_count","pushes current builtin argument count","exact"
        },
        {
            24,0x55FB88,0x47C6A0,"loc_47C6A0","current_process_state","pushes state of current process","recovered"
        },
        {
            25,0x55FB8C,0x47C780,"loc_47C780","push_zero","pushes integer zero","exact"
        },
        {
            26,0x55FB90,0x47C7E0,"loc_47C7E0","send_message_marshaled","VM message-send marshaller","partial"
        },
        {
            27,0x55FB94,0x47C780,"loc_47C780","push_zero_alias","alias of push_zero","exact"
        },
        {
            28,0x55FB98,0x47C1A0,"loc_47C1A0","ffprc_id","resolves process id by name","recovered"
        },
        {
            29,0x55FB9C,0x47D290,"loc_47D290","push_context_id_or_zero","pushes context id or zero","exact"
        },
        {
            30,0x55FBA0,0x47D2C0,"loc_47D2C0","lookup_process_by_name","looks up process/name","recovered"
        },
        {
            31,0x55FBA4,0x477C10,"loc_477C10","external_runtime_update_473730","tail-jumps to external runtime update","external"
        },
        {
            32,0x55FBA8,0x47D350,"loc_47D350","push_current_flags_mask_4","pushes current flags mask","exact"
        },
        {
            33,0x55FBAC,0x47CB20,"loc_47CB20","receive_message_marshaled","VM receive/unmarshal path","partial"
        },
        {
            34,0x55FBB0,0x47D3B0,"loc_47D3B0","strcpy_checked","bounded string copy into VM slice","recovered"
        },
        {
            35,0x55FBB4,0x47D5C0,"loc_47D5C0","strcat_checked","bounded strcat into VM slice","exact"
        },
        {
            36,0x55FBB8,0x47D890,"loc_47D890","strlen_checked","pushes string length","exact"
        },
        {
            37,0x55FBBC,0x47D9C0,"loc_47D9C0","strcmp","pushes strcmp(a,b)","exact"
        },
        {
            38,0x55FBC0,0x47DBE0,"loc_47DBE0","log_event_dispatch","logging/statistics dispatcher","partial"
        },
        {
            39,0x55FBC4,0x47C710,"loc_47C710","current_process_id","pushes current process id","recovered"
        },
        {
            40,0x55FBC8,0x47E500,"loc_47E500","file_create","creates/truncates a file","recovered"
        },
        {
            41,0x55FBCC,0x47E590,"loc_47E590","file_open","opens a file","recovered"
        },
        {
            42,0x55FBD0,0x47E6A0,"loc_47E6A0","file_close","closes a file descriptor","exact"
        },
        {
            43,0x55FBD4,0x47E6D0,"loc_47E6D0","file_write","writes VM buffer to file","exact"
        },
        {
            44,0x55FBD8,0x47E750,"loc_47E750","file_read","reads file into VM buffer","exact"
        },
        {
            45,0x55FBDC,0x47EA60,"loc_47EA60","identity_int","identity integer","exact"
        },
        {
            46,0x55FBE0,0x47EAC0,"loc_47EAC0","identity_float","identity float","exact"
        },
        {
            47,0x55FBE4,0x47EB40,"loc_47EB40","object_create","creates engine object","recovered"
        },
        {
            48,0x55FBE8,0x47EC50,"loc_47EC50","object_set_pos_xyz","sets object position fields","recovered"
        },
        {
            49,0x55FBEC,0x47F0A0,"loc_47F0A0","object_add_pos_xyz","adds to object position","exact"
        },
        {
            50,0x55FBF0,0x47F120,"loc_47F120","view_set_pos_xyz","sets view position","recovered"
        },
        {
            51,0x55FBF4,0x47F180,"loc_47F180","view_set_z","sets view z","recovered"
        },
        {
            52,0x55FBF8,0x47EEB0,"loc_47EEB0","object_set_vec14_xyz","sets object vec14","exact"
        },
        {
            53,0x55FBFC,0x47F1C0,"loc_47F1C0","global_vector_set","sets global vector","recovered"
        },
        {
            54,0x55FC00,0x47ED00,"loc_47ED00","object_get_x","pushes object x","recovered"
        },
        {
            55,0x55FC04,0x47ED40,"loc_47ED40","object_get_y","pushes object y","recovered"
        },
        {
            56,0x55FC08,0x47ED80,"loc_47ED80","object_get_z","pushes object z","recovered"
        },
        {
            57,0x55FC0C,0x47EDC0,"loc_47EDC0","object_get_vec14_x","pushes vector x","exact"
        },
        {
            58,0x55FC10,0x47EE00,"loc_47EE00","object_get_vec14_y","pushes vector y","exact"
        },
        {
            59,0x55FC14,0x47EE40,"loc_47EE40","object_get_vec14_z","pushes vector z","exact"
        },
        {
            60,0x55FC18,0x47EE80,"loc_47EE80","object_delete_type0","deletes object handle type0","recovered"
        },
        {
            61,0x55FC1C,0x47F270,"loc_47F270","text_api","text object API","partial"
        },
        {
            62,0x55FC20,0x47F3F0,"loc_47F3F0","object_release_type4","releases object handle type4","recovered"
        },
        {
            63,0x55FC24,0x47F420,"loc_47F420","text_color","sets text object color","recovered"
        },
        {
            64,0x55FC28,0x47EAD0,"loc_47EAD0","push_runtime_slot","pushes runtime slot","exact"
        },
        {
            65,0x55FC2C,0x47E870,"loc_47E870","file_seek","lseek wrapper","exact"
        },
        {
            66,0x55FC30,0x47E8C0,"loc_47E8C0","file_length","pushes filelength","exact"
        },
        {
            67,0x55FC34,0x47F720,"loc_47F720","sprintf","formats into VM string buffer","exact"
        },
        {
            68,0x55FC38,0x47E9F0,"loc_47E9F0","file_rename","rename wrapper","exact"
        },
        {
            69,0x55FC3C,0x47E9C0,"loc_47E9C0","file_remove","remove wrapper","exact"
        },
        {
            70,0x55FC40,0x47E8E0,"loc_47E8E0","file_stat_time_field","pushes fstat time field","recovered"
        },
        {
            71,0x55FC44,0x47E930,"loc_47E930","file_truncate","chsize wrapper","exact"
        },
        {
            72,0x55FC48,0x47E960,"loc_47E960","file_set_time","sets file time","exact"
        },
        {
            73,0x55FC4C,0x47F470,"loc_47F470","sprite_create_or_update","sprite/resource creation/update","recovered"
        },
        {
            74,0x55FC50,0x47EA30,"loc_47EA30","file_lookup_476310","file lookup/status","recovered"
        },
        {
            75,0x55FC54,0x47DA60,"loc_47DA60","stricmp","case-insensitive compare","exact"
        },
        {
            76,0x55FC58,0x47DAE0,"loc_47DAE0","strncmp","strncmp compare","exact"
        },
        {
            77,0x55FC5C,0x47F9D0,"loc_47F9D0","process_memcpy","copy between process address spaces","recovered"
        },
        {
            78,0x55FC60,0x47FBF0,"loc_47FBF0","memcpy","memcpy with VM slice checks","exact"
        },
        {
            79,0x55FC64,0x47FCD0,"loc_47FCD0","memset","memset with VM slice checks","exact"
        },
        {
            80,0x55FC68,0x47D200,"loc_47D200","angle_delta","wrapped angular delta","recovered"
        },
        {
            81,0x55FC6C,0x47BBB0,"loc_47BBB0","distance_or_distance_sq","distance helpers","exact"
        },
        {
            82,0x55FC70,0x47FD40,"loc_47FD40","request_halt","requests interpreter/application halt","recovered"
        },
        {
            83,0x55FC74,0x47EF30,"loc_47EF30","object_state_query","object state query","recovered"
        },
        {
            84,0x55FC78,0x47F060,"loc_47F060","object_get_field_0xB4","pushes object field","exact"
        },
        {
            85,0x55FC7C,0x47FD70,"loc_47FD70","push_global_55F73C","pushes global integer","recovered"
        },
        {
            86,0x55FC80,0x47E4D0,"loc_47E4D0","discard_value","pops/discards one argument","exact"
        },
        {
            87,0x55FC84,0x47FDD0,"loc_47FDD0","object_set_int_property_a","sets object int property A","recovered"
        },
        {
            88,0x55FC88,0x47FE30,"loc_47FE30","object_set_int_property_b","sets object int property B","recovered"
        },
        {
            89,0x55FC8C,0x47FE90,"loc_47FE90","object_relation_query","object relation query","recovered"
        },
        {
            90,0x55FC90,0x47FEC0,"loc_47FEC0","object_set_float_property","sets object float property","recovered"
        },
        {
            91,0x55FC94,0x47E320,"loc_47E320","formatted_file_log","timestamped formatted file log","exact"
        },
        {
            92,0x55FC98,0x47FEF0,"loc_47FEF0","object_set_float_fields_0x27C_0x280_0x284","sets object float fields","exact"
        },
        {
            93,0x55FC9C,0x47FFA0,"loc_47FFA0","object_set_float_field_0x28C","sets object float field 0x28C","exact"
        },
        {
            94,0x55FCA0,0x480010,"loc_480010","object_set_float_field_0x294","sets object float field 0x294","exact"
        },
        {
            95,0x55FCA4,0x47D7B0,"loc_47D7B0","strstr","substring search","exact"
        },
        {
            96,0x55FCA8,0x47FC60,"loc_47FC60","memmove","memmove with VM checks","exact"
        },
        {
            97,0x55FCAC,0x480070,"loc_480070","object_get_or_set_flag_0x278","object flag get/set","exact"
        },
        {
            98,0x55FCB0,0x4800F0,"loc_4800F0","push_runtime_constant_pair","pushes runtime constants","exact"
        },
        {
            99,0x55FCB4,0x47F220,"loc_47F220","object_set_flag_0x141","sets object flag","exact"
        },
        {
            100,0x55FCB8,0x480150,"loc_480150","object_get_norm_vec3","writes norm vector","exact"
        },
        {
            101,0x55FCBC,0x480220,"loc_480220","object_get_position_vec3","writes position vector","recovered"
        },
        {
            102,0x55FCC0,0x4802C0,"loc_4802C0","object_get_abg_vec3","writes abg vector","exact"
        },
        {
            103,0x55FCC4,0x483C70,"loc_483C70","ffsys_api","large ffsys selector switch","partial"
        },
        {
            104,0x55FCC8,0x4819D0,"loc_4819D0","thisname","copies current script/object name","recovered"
        },
        {
            105,0x55FCCC,0x47F580,"loc_47F580","object_remove_type5","removes type5 handle","recovered"
        },
        {
            106,0x55FCD0,0x47BB80,"loc_47BB80","rand_float","pushes random float","exact"
        },
        {
            107,0x55FCD4,0x481A20,"loc_481A20","prc_name","copies process name","recovered"
        },
        {
            108,0x55FCD8,0x47FAF0,"loc_47FAF0","ffmempcpy_alt","alternate process memory copy","recovered"
        },
        {
            109,0x55FCDC,0x481AA0,"loc_481AA0","copy_effect_name_by_id","copies effect name","exact"
        },
        {
            110,0x55FCE0,0x481B10,"loc_481B10","find_effect_id","finds effect id","recovered"
        },
        {
            111,0x55FCE4,0x481BC0,"loc_481BC0","effect_attach","attaches effect","recovered"
        },
        {
            112,0x55FCE8,0x481E80,"loc_481E80","dmalloc_free","frees dynamic VM allocation","recovered"
        },
        {
            113,0x55FCEC,0x481D70,"loc_481D70","dmalloc","allocates dynamic VM memory","recovered"
        },
        {
            114,0x55FCF0,0x481F60,"loc_481F60","assoc_array_set","runtime associative array set","recovered"
        },
        {
            115,0x55FCF4,0x4820E0,"loc_4820E0","assoc_array_get","runtime associative array get","recovered"
        },
        {
            116,0x55FCF8,0x47E650,"loc_47E650","file_lock","locking wrapper","exact"
        },
        {
            117,0x55FCFC,0x486A60,"sub_486A60","native_config_api","native config selector dispatcher","recovered"
        },
        {
            118,0x55FD00,0x4822B0,"loc_4822B0","push_static_word_span","pushes static buffer span","recovered"
        },
        {
            119,0x55FD04,0x4822E0,"loc_4822E0","push_minus_one","pushes integer -1","exact"
        },
        {
            120,0x55FD08,0x482330,"sub_482330","raw_arg_read","raw/native argument reader","recovered"
        },
        {
            121,0x55FD0C,0x47F820,"loc_47F820","process_translate_ptr","translates process memory pointer","recovered"
        },
        {
            122,0x55FD10,0x46B620,"nullsub_1","reserved_noop_7a","reserved disabled builtin","recovered"
        },
        {
            123,0x55FD14,0x480340,"loc_480340","snprintf","bounded vsnprintf into VM string buffer","exact"
        },
        {
            124,0x55FD18,0x47E7C0,"loc_47E7C0","file_read_line","line-oriented read","exact"
        },
        {
            125,0x55FD1C,0x47BA20,"loc_47BA20","exp","pushes exp(float_arg)","exact"
        },
        {
            126,0x55FD20,0x47E030,"loc_47E030","logf","formatted runtime logging","recovered"
        },
        {
            127,0x55FD24,0x47F7B0,"loc_47F7B0","current_sender_id","pushes current sender/process id","recovered"
        },
        {
            128,0x55FD28,0x483C00,"loc_483C00","parse_api","nested parser dispatcher","recovered"
        },
        {
            129,0x55FD2C,0x48AE50,"loc_48AE50","chat_utility_api","chat utility selector dispatcher","external"
        },
        {
            130,0x55FD30,0x46B620,"nullsub_1","reserved_noop_82","reserved disabled builtin","recovered"
        },
        {
            131,0x55FD34,0x4803E0,"loc_4803E0","editor_get_click_point","editor click point API","recovered"
        },
        {
            132,0x55FD38,0x489C10,"loc_489C10","item_inventory_api","large item/inventory dispatcher","partial"
        },
        {
            133,0x55FD3C,0x46B620,"nullsub_1","reserved_noop_85","reserved disabled builtin","recovered"
        },
        {
            134,0x55FD40,0x47BE30,"loc_47BE30","strchr","character search","exact"
        },
        {
            135,0x55FD44,0x47D820,"loc_47D820","stristr","case-insensitive substring search","recovered"
        },
        {
            136,0x55FD48,0x47D4B0,"loc_47D4B0","strncpy_checked","bounded strncpy into VM slice","exact"
        },
        {
            137,0x55FD4C,0x47DB60,"loc_47DB60","strnicmp","case-insensitive bounded compare","exact"
        },
        {
            138,0x55FD50,0x482340,"loc_482340","bit_and","integer bitwise AND","exact"
        },
        {
            139,0x55FD54,0x4823B0,"loc_4823B0","bit_or","integer bitwise OR","exact"
        },
        {
            140,0x55FD58,0x482420,"loc_482420","bit_xor","integer bitwise XOR","exact"
        },
        {
            141,0x55FD5C,0x482490,"loc_482490","bit_not","integer bitwise NOT","exact"
        },
        {
            142,0x55FD60,0x4824F0,"loc_4824F0","shift_left","integer left shift","exact"
        },
        {
            143,0x55FD64,0x482560,"loc_482560","shift_right","integer right shift","exact"
        },
        {
            144,0x55FD68,0x4825D0,"loc_4825D0","bit_clear","clears indexed bit","exact"
        },
        {
            145,0x55FD6C,0x482650,"loc_482650","bit_set","sets indexed bit","exact"
        },
        {
            146,0x55FD70,0x4826D0,"loc_4826D0","bit_test","tests indexed bit","exact"
        },
        {
            147,0x55FD74,0x487490,"loc_487490","memcmp","buffer compare","exact"
        },
        {
            148,0x55FD78,0x4828B0,"loc_4828B0","typed_load_width_1","external typed load helper width 1","external"
        },
        {
            149,0x55FD7C,0x4828C0,"loc_4828C0","typed_load_width_2","external typed load helper width 2","external"
        },
        {
            150,0x55FD80,0x4828D0,"loc_4828D0","typed_load_width_3","external typed load helper width 3","external"
        },
        {
            151,0x55FD84,0x4828E0,"loc_4828E0","typed_load_width_4","external typed load helper width 4","external"
        },
        {
            152,0x55FD88,0x4828F0,"loc_4828F0","span_write_float","writes float into destination slice","recovered"
        },
        {
            153,0x55FD8C,0x482990,"loc_482990","span_write_cstring","copies cstring into destination slice","recovered"
        },
        {
            154,0x55FD90,0x482AD0,"loc_482AD0","typed_store_width_1","external typed store helper width 1","external"
        },
        {
            155,0x55FD94,0x482AE0,"loc_482AE0","typed_store_width_2","external typed store helper width 2","external"
        },
        {
            156,0x55FD98,0x482AF0,"loc_482AF0","typed_store_width_3","external typed store helper width 3","external"
        },
        {
            157,0x55FD9C,0x482B00,"loc_482B00","typed_store_width_4","external typed store helper width 4","external"
        },
        {
            158,0x55FDA0,0x482B10,"loc_482B10","ptr_store_i32_from_ptr","copies i32 from ptr to ptr","recovered"
        },
        {
            159,0x55FDA4,0x482BD0,"loc_482BD0","ptr_copy_cstring","copies cstring from pointer descriptor","recovered"
        },
        {
            160,0x55FDA8,0x482CC0,"loc_482CC0","binary_search_i32","binary-searches sorted int32 array","recovered"
        },
        {
            161,0x55FDAC,0x454780,"loc_454780","resource_handle_api","nested resource-handle dispatcher","partial"
        },
        {
            162,0x55FDB0,0x442040,"loc_442040","entity_ref_api","nested entity/reference API","recovered"
        },
        {
            163,0x55FDB4,0x482DA0,"loc_482DA0","buffer_hash_or_checksum","buffer hash/checksum helper","recovered"
        }
    };
}
const std::vector<FMbcOpcodeSpec>& MbcOpcodeTable()
{
    static auto table = MakeOpcodes();
    return table;
}
const std::vector<FMbcBuiltinSpec>& MbcBuiltinTable()
{
    static auto table = MakeBuiltins();
    return table;
}
const FMbcOpcodeSpec* FindMbcOpcode(uint8 opcode)
{
    for (const auto& spec : MbcOpcodeTable())
    {
        if (spec.Opcode == opcode)
        {
            return &spec;
        }
    }

    return nullptr;
}
const FMbcBuiltinSpec* FindMbcBuiltin(uint8 subOpcode)
{
    for (const auto& spec : MbcBuiltinTable())
    {
        if (spec.SubOpcode == subOpcode)
        {
            return &spec;
        }
    }

    return nullptr;
}
FMbcDecodedOpcode DecodeMbcOpcode(const FByteArray& code, uint32 off)
{
    FMbcDecodedOpcode out;

    if (off >= code.size())
    {
        out.Mnemonic = "eof";
        out.Length = 0;
        out.Terminal = true;
        out.Known = false;
        return out;
    }

    uint8 opcode = code[off];

    if (opcode == 0x23)
    {
        out.Mnemonic = "end_program";
        out.Length = 1;
        out.Terminal = true;
        out.Edges.push_back({"end_program", std::nullopt, "main-loop special: finish current program"});
        return out;
    }

    if (opcode == 0x7C)
    {
        out.Mnemonic = "yield_program";
        out.Length = 1;
        out.Terminal = true;
        out.Operands["resume_target"] = U(off + 1);
        out.Edges.push_back({"yield_resume", off + 1, "saved PC after yield; resumed by scheduler"});
        return out;
    }

    const FMbcOpcodeSpec* spec = FindMbcOpcode(opcode);

    if (!spec)
    {
        out.Mnemonic = "unknown_" + H(opcode);
        out.Length = 1;
        out.Terminal = true;
        out.Known = false;
        out.Edges.push_back({"trap", std::nullopt, "opcode absent in recovered dispatcher table"});
        return out;
    }

    out.Spec = spec;
    out.Mnemonic = spec->Mnemonic;
    out.Operands["handler"] = spec->HandlerName;
    out.Operands["handler_ea"] = H(spec->HandlerEa);
    out.Operands["semantic"] = spec->Semantic;
    auto have = [&](uint32 n)
    {
        return off <= code.size() && n <= code.size() - off;
    };

    try
    {
        switch (spec->Format)
        {
        case EMbcOperandFormat::None: out.Length = 1;
            break;
        case EMbcOperandFormat::Trap: out.Length = 1;
            out.Terminal = true;
            out.Known = false;
            out.Edges.push_back({"trap", std::nullopt, spec->Semantic});
            break;
        case EMbcOperandFormat::U8: if (!have(2))
            {
                throw std::runtime_error("truncated u8");
            }
            out.Length = 2;
            out.Operands["value"] = U(code[off + 1]);
            break;
        case EMbcOperandFormat::U16: if (!have(3))
            {
                throw std::runtime_error("truncated u16");
            }
            out.Length = 3;
            out.Operands["value"] = U(Mbc::ReadU16(code, off + 1));
            break;
        case EMbcOperandFormat::ProgramI16: if (!have(3))
            {
                throw std::runtime_error("truncated program_i16");
            }
            out.Length = 3;
            out.Operands["program_index"] = S(Mbc::ReadI16(code, off + 1));
            break;
        case EMbcOperandFormat::Rel16:
            {
                if (!have(3))
                {
                    throw std::runtime_error("truncated rel16");
                }

                out.Length = 3;
                int32 rel = Mbc::ReadI16(code, off + 1);
                uint32 target = static_cast<uint32>(int32(off + 1) + rel);
                out.Operands["rel"] = S(rel);
                out.Operands["target"] = U(target);
                out.Edges.push_back({"jmp", target, ""});
                out.Terminal = true;
                break;
            }
        case EMbcOperandFormat::Rel32:
            {
                if (!have(5))
                {
                    throw std::runtime_error("truncated rel32");
                }

                out.Length = 5;
                int32 rel = Mbc::ReadI32(code, off + 1);
                uint32 target = static_cast<uint32>(int32(off + 1) + rel);
                out.Operands["rel"] = S(rel);
                out.Operands["target"] = U(target);
                out.Edges.push_back({"jmp", target, ""});
                out.Terminal = true;
                break;
            }
        case EMbcOperandFormat::JFalseRel16:
            {
                if (!have(3))
                {
                    throw std::runtime_error("truncated jfalse_rel16");
                }

                out.Length = 3;
                int32 rel = Mbc::ReadI16(code, off + 1);
                uint32 target = static_cast<uint32>(int32(off + 1) + rel);
                out.Operands["rel"] = S(rel);
                out.Operands["target"] = U(target);
                out.Operands["fallthrough"] = U(off + out.Length);
                out.Edges.push_back({"jfalse", target, "condition false/zero"});
                out.Edges.push_back({"jtrue_fallthrough", off + out.Length, "condition true/non-zero"});
                break;
            }
        case EMbcOperandFormat::JFalseRel32:
            {
                if (!have(5))
                {
                    throw std::runtime_error("truncated jfalse_rel32");
                }

                out.Length = 5;
                int32 rel = Mbc::ReadI32(code, off + 1);
                uint32 target = static_cast<uint32>(int32(off + 1) + rel);
                out.Operands["rel"] = S(rel);
                out.Operands["target"] = U(target);
                out.Operands["fallthrough"] = U(off + out.Length);
                out.Edges.push_back({"jfalse", target, "condition false/zero"});
                out.Edges.push_back({"jtrue_fallthrough", off + out.Length, "condition true/non-zero"});
                break;
            }
        case EMbcOperandFormat::LogicalOrRel16:
            {
                if (!have(3))
                {
                    throw std::runtime_error("truncated or_rel16");
                }

                out.Length = 3;
                int32 rel = Mbc::ReadI16(code, off + 1);
                uint32 target = static_cast<uint32>(int32(off + 1) + rel);
                out.Operands["target"] = U(target);
                out.Edges.push_back({"jtrue", target, "short-circuit OR"});
                out.Edges.push_back({"jfalse_fallthrough", off + out.Length, "false falls through"});
                break;
            }
        case EMbcOperandFormat::LogicalAndRel16:
            {
                if (!have(3))
                {
                    throw std::runtime_error("truncated and_rel16");
                }

                out.Length = 3;
                int32 rel = Mbc::ReadI16(code, off + 1);
                uint32 target = static_cast<uint32>(int32(off + 1) + rel);
                out.Operands["target"] = U(target);
                out.Edges.push_back({"jfalse", target, "short-circuit AND"});
                out.Edges.push_back({"jtrue_fallthrough", off + out.Length, "true falls through"});
                break;
            }
        case EMbcOperandFormat::CallRel32:
            {
                if (!have(5))
                {
                    throw std::runtime_error("truncated call_rel32");
                }

                out.Length = 5;
                int32 rel = Mbc::ReadI32(code, off + 1);
                uint32 target = static_cast<uint32>(int32(off + 1) + rel);
                out.Operands["target"] = U(target);
                out.Operands["return"] = U(off + out.Length);
                out.Edges.push_back({"call_rel32", target, ""});
                out.Edges.push_back({"call_return", off + out.Length, ""});
                break;
            }
        case EMbcOperandFormat::DataRef: if (!have(6))
            {
                throw std::runtime_error("truncated data_ref");
            }
            out.Length = 6;
            out.Operands["type"] = U(code[off + 1]);
            out.Operands["type_name"] = Mbc::TypeName(code[off + 1]);
            out.Operands["data_offset"] = U(Mbc::ReadU32(code, off + 2));
            break;
        case EMbcOperandFormat::TypedImm32: if (!have(6))
            {
                throw std::runtime_error("truncated typed_imm32");
            }
            out.Length = 6;
            out.Operands["type"] = U(code[off + 1]);
            out.Operands["type_name"] = Mbc::TypeName(code[off + 1]);
            out.Operands["value_u32"] = U(Mbc::ReadU32(code, off + 2));
            out.Operands["value_i32"] = S(Mbc::ReadI32(code, off + 2));

            if (code[off + 1] == Mbc::TypeFloat)
            {
                out.Operands["value_float"] = std::to_string(Mbc::FloatFromU32(Mbc::ReadU32(code, off + 2)));
            }

            break;
        case EMbcOperandFormat::TypedImmU16: if (!have(4))
            {
                throw std::runtime_error("truncated typed_imm_u16");
            }
            out.Length = 4;
            out.Operands["type"] = U(code[off + 1]);
            out.Operands["type_name"] = Mbc::TypeName(code[off + 1]);
            out.Operands["value"] = U(Mbc::ReadU16(code, off + 2));
            break;
        case EMbcOperandFormat::TypedImmI8: if (!have(3))
            {
                throw std::runtime_error("truncated typed_imm_i8");
            }
            out.Length = 3;
            out.Operands["type"] = U(code[off + 1]);
            out.Operands["type_name"] = Mbc::TypeName(code[off + 1]);
            out.Operands["value"] = S(Mbc::Sign8(code[off + 2]));
            break;
        case EMbcOperandFormat::ImportStubU32: if (!have(5))
            {
                throw std::runtime_error("truncated import_stub");
            }
            out.Length = 5;
            out.Terminal = true;
            out.Operands["payload_u32"] = U(Mbc::ReadU32(code, off + 1));
            out.Edges.push_back({"import_stub", std::nullopt, spec->Semantic});
            break;
        case EMbcOperandFormat::InlineSpan: if (!have(7))
            {
                throw std::runtime_error("truncated inline_span");
            }
            out.Length = 7;
            out.Operands["data_offset"] = U(Mbc::ReadU32(code, off + 1));
            out.Operands["length"] = U(Mbc::ReadU16(code, off + 5));
            break;
        case EMbcOperandFormat::TypedSpanRef: case EMbcOperandFormat::TypedSpanInline: if (!have(10))
            {
                throw std::runtime_error("truncated typed_span");
            }
            out.Length = 10;
            out.Operands["type"] = U(code[off + 1]);
            out.Operands["type_name"] = Mbc::TypeName(code[off + 1]);
            out.Operands["data_offset"] = U(Mbc::ReadU32(code, off + 2));
            out.Operands["length"] = U(Mbc::ReadU32(code, off + 6));
            break;
        case EMbcOperandFormat::ArrayAbs: if (!have(16))
            {
                throw std::runtime_error("truncated array_abs");
            }
            out.Length = 16;
            out.Operands["type"] = U(code[off + 1]);
            out.Operands["type_name"] = Mbc::TypeName(code[off + 1]);
            out.Operands["element_size"] = U(Mbc::ReadU16(code, off + 2));
            out.Operands["base"] = U(Mbc::ReadU32(code, off + 4));
            out.Operands["span"] = U(Mbc::ReadU32(code, off + 8));
            out.Operands["count"] = S(Mbc::ReadI32(code, off + 12));
            break;
        case EMbcOperandFormat::Array2Checked: if (!have(8))
            {
                throw std::runtime_error("truncated array2_checked");
            }
            out.Length = 8;
            out.Operands["type"] = U(code[off + 1]);
            out.Operands["type_name"] = Mbc::TypeName(code[off + 1]);
            out.Operands["element_size"] = U(Mbc::ReadU16(code, off + 2));
            out.Operands["count"] = S(Mbc::ReadI32(code, off + 4));
            break;
        case EMbcOperandFormat::Array2: if (!have(4))
            {
                throw std::runtime_error("truncated array2");
            }
            out.Length = 4;
            out.Operands["type"] = U(code[off + 1]);
            out.Operands["type_name"] = Mbc::TypeName(code[off + 1]);
            out.Operands["element_size"] = U(Mbc::ReadU16(code, off + 2));
            break;
        case EMbcOperandFormat::SliceOffsetRef: if (!have(4))
            {
                throw std::runtime_error("truncated slice_offset_ref");
            }
            out.Length = code[off + 1] == Mbc::TypeSlice ? 8 : 4;

            if (!have(out.Length))
            {
                throw std::runtime_error("truncated slice_offset_ref_ext");
            }

            out.Operands["type"] = U(code[off + 1]);
            out.Operands["type_name"] = Mbc::TypeName(code[off + 1]);
            out.Operands["offset"] = U(Mbc::ReadU16(code, off + 2));

            if (out.Length == 8)
            {
                out.Operands["length"] = U(Mbc::ReadU32(code, off + 4));
            }

            break;
        case EMbcOperandFormat::SliceOffsetSpan: if (!have(8))
            {
                throw std::runtime_error("truncated slice_offset_span");
            }
            out.Length = 8;
            out.Operands["type"] = U(code[off + 1]);
            out.Operands["type_name"] = Mbc::TypeName(code[off + 1]);
            out.Operands["offset"] = U(Mbc::ReadU16(code, off + 2));
            out.Operands["length"] = U(Mbc::ReadU32(code, off + 4));
            break;
        case EMbcOperandFormat::Prologue:
            {
                if (!have(2))
                {
                    throw std::runtime_error("truncated prologue count");
                }

                int8 signedCount = Mbc::Sign8(code[off + 1]);
                uint32 count = signedCount < 0 ? uint32(-signedCount) : uint32(signedCount);
                out.Length = 2 + count * 5;

                if (!have(out.Length))
                {
                    throw std::runtime_error("truncated prologue descriptors");
                }

                out.Operands["signed_count"] = S(signedCount);
                out.Operands["descriptor_count"] = U(count);

                for (uint32 i = 0; i < count; ++i)
                {
                    uint32 p = off + 2 + i * 5;
                    out.Operands["arg" + U(i) + "_type"] = U(code[p]);
                    out.Operands["arg" + U(i) + "_data_offset"] = U(Mbc::ReadU32(code, p + 1));
                }

                break;
            }
        case EMbcOperandFormat::Builtin:
            {
                if (!have(2))
                {
                    throw std::runtime_error("truncated builtin");
                }

                out.Length = 2;
                uint8 sub = code[off + 1];
                const FMbcBuiltinSpec* builtin = FindMbcBuiltin(sub);
                out.Builtin = builtin;
                out.Operands["subopcode"] = U(sub);

                if (builtin)
                {
                    out.Mnemonic = builtin->Mnemonic;
                    out.Operands["target_ea"] = H(builtin->TargetEa);
                    out.Operands["target_name"] = builtin->TargetName;
                    out.Operands["builtin_semantic"] = builtin->Semantic;
                    out.Operands["builtin_confidence"] = builtin->Confidence;
                }
                else
                {
                    out.Mnemonic = "unknown_builtin_" + H(sub);
                    out.Known = false;
                }

                break;
            }
        }
    }
    catch (const std::exception& e)
    {
        out.Length = 1;
        out.Known = false;
        out.Terminal = false;
        out.Operands["decode_error"] = e.what();
        out.Mnemonic += "_truncated";
        out.Edges.clear();
    }

    if (opcode == 0x72 || opcode == 0x74)
    {
        out.Terminal = true;
        out.Edges.push_back({"return", std::nullopt, spec->Semantic});
    }

    if (opcode == 0x48)
    {
        out.Terminal = true;
        out.Edges.push_back({"halt", std::nullopt, spec->Semantic});
    }

    return out;
}
