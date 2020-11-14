; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb gl -uri sqlite:%t.bcdb prog -o %t --merged-name=libmerged.so

%struct.object = type { i8*, i8*, i8*, %struct.object*, %struct.object*, %struct.object*, %struct.monster_race* }
%struct.monster_race = type { %struct.monster_race*, i32, i8* }
%struct.monster = type opaque

define %struct.object* @object_new() {
  ret %struct.object* undef
}

declare void @object_copy(%struct.object*, %struct.object*)

define internal i32 @artifact_power(i32, i8*, i1 zeroext) {
  %4 = call %struct.object* @object_new()
  call void @object_copy(%struct.object* undef, %struct.object* undef)
  %5 = getelementptr inbounds %struct.object, %struct.object* undef, i32 0, i32 5
  ret i32 undef
}

define internal void @wr_monster(%struct.monster*) {
  %2 = call %struct.object* @object_new()
  %3 = getelementptr inbounds %struct.monster_race, %struct.monster_race* undef, i32 0, i32 2
  %4 = getelementptr inbounds %struct.object, %struct.object* undef, i32 0, i32 4
  ret void
}
