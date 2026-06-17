import 'package:flutter/material.dart';

import 'screens/home_shell.dart';

void main() {
  runApp(const RapidPlusApp());
}

class RapidPlusApp extends StatelessWidget {
  const RapidPlusApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'FBT_RAPID',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        useMaterial3: true,
        colorSchemeSeed: const Color(0xFF1565C0),
        brightness: Brightness.light,
        // Mọi thông báo (SnackBar) có nút X để tắt, ghim sát đáy màn hình.
        snackBarTheme: const SnackBarThemeData(
          behavior: SnackBarBehavior.fixed,
          showCloseIcon: true,
        ),
      ),
      home: const HomeShell(),
    );
  }
}
