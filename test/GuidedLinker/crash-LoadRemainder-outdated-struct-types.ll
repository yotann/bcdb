; RUN: memodb init -store sqlite:%t.bcdb
; RUN: bcdb add -store sqlite:%t.bcdb %s -name prog
; RUN: bcdb gl -store sqlite:%t.bcdb prog -o %t --merged-name=libmerged.so

%struct.monster_list_s = type { %struct.monster_list_entry_s*, i64, i16, i32 }
%struct.monster_list_entry_s = type { i8 }

@cave = external local_unnamed_addr global i8*, align 8
@monster_list_subwindow = internal unnamed_addr global %struct.monster_list_s* null, align 8

define %struct.monster_list_s* @monster_list_shared_instance() local_unnamed_addr  {
  %1 = load %struct.monster_list_s*, %struct.monster_list_s** @monster_list_subwindow, align 8
  ret %struct.monster_list_s* %1
}

define void @monster_list_show_subwindow() local_unnamed_addr  {
  call %struct.monster_list_s* @monster_list_shared_instance()
  load i8*, i8** @cave, align 8
  getelementptr inbounds %struct.monster_list_s, %struct.monster_list_s* undef, i32 0, i32 3
  ret void
}
