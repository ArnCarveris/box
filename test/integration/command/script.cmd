script_read script_content.flecs
script_patch script_content.flecs "@@ -8,4 +8,8 @@
   b {
     Position: {2, 0, 0}
   }
+
+  c {
+    Position: {4, 0, 0}
+  }
 }"
ls path
script_grep Position
