import 'package:flutter_test/flutter_test.dart';

import 'package:RapidPlusApp/models/user_session.dart';

/// Ma trận quyền của 5 vai trò (docs/plan/tai-khoan-nha-may.md §3.1).
///
/// Đây là test đáng giá nhất trong repo: một ô sai ở bảng này là trao quyền nạp
/// firmware cho cả fleet vào tay người đứng máy ở xưởng, và không màn hình nào
/// báo cho biết.

UserSession _s(UserRole role, {List<String> ids = const []}) => UserSession(
      username: role.name,
      name: role.name,
      role: role,
      ids: ids,
      allowAll: role == UserRole.root || ids.contains('*'),
    );

void main() {
  final root = _s(UserRole.root);
  final admin = _s(UserRole.admin, ids: ['RPL02013']);
  final manager = _s(UserRole.manager);
  final operator = _s(UserRole.operator);
  final customer = _s(UserRole.user, ids: ['RPL02013']);

  group('mã vai trò đi/về backend', () {
    test('khớp _ROLES của server', () {
      expect(
          [for (final r in UserRole.values) roleCodeOf(r)],
          ['root', 'admin', 'manager', 'operator', 'user']);
      for (final r in UserRole.values) {
        expect(roleFromCode(roleCodeOf(r)), r);
      }
    });

    test('vai trò lạ → khách hàng (fail-closed, mất quyền chứ không thừa)', () {
      for (final v in ['', 'ADMINISTRATOR', 'superuser', null, 42]) {
        expect(roleFromCode(v), UserRole.user, reason: 'với $v');
      }
      expect(roleFromCode('MANAGER'), UserRole.manager); // hoa/thường vẫn nhận
    });
  });

  group('người của xưởng KHÔNG chạm dữ liệu lâm sàng', () {
    test('không xem, không ghi', () {
      for (final s in [manager, operator]) {
        expect(s.canSeeClinical, isFalse, reason: s.roleCode);
        expect(s.canWriteClinical, isFalse, reason: s.roleCode);
        expect(s.canSupport, isFalse, reason: s.roleCode);
      }
      expect(admin.canSeeClinical, isTrue);
      expect(customer.canSeeClinical, isTrue);
      expect(customer.canWriteClinical, isFalse); // khách hàng vẫn read-only
    });
  });

  group('quyền ghi firmware KHÔNG xuống tới xưởng', () {
    test('chỉ root/admin ghi OTA và dùng tab Kỹ Thuật', () {
      expect([root.canWriteOta, admin.canWriteOta], [true, true]);
      for (final s in [manager, operator, customer]) {
        expect(s.canWriteOta, isFalse, reason: s.roleCode);
        expect(s.canUseTech, isFalse, reason: s.roleCode);
      }
    });

    test('quản lý sản xuất XEM được trạng thái máy, thao tác viên thì không', () {
      expect(manager.canSeeOta, isTrue);
      expect(operator.canSeeOta, isFalse);
      expect(customer.canSeeOta, isFalse);
    });
  });

  group('tab Sản xuất', () {
    test('bốn vai trò chạy được trạm, khách hàng thì không', () {
      for (final s in [root, admin, manager, operator]) {
        expect(s.canRunStation, isTrue, reason: s.roleCode);
        expect(s.canSeeProduction, isTrue, reason: s.roleCode);
      }
      expect(customer.canRunStation, isFalse);
      expect(customer.canSeeProduction, isFalse);
    });

    test('Thống kê: thao tác viên KHÔNG thấy', () {
      expect(manager.canSeeProductionStats, isTrue);
      expect(admin.canSeeProductionStats, isTrue);
      expect(operator.canSeeProductionStats, isFalse);
    });

    test('hồ sơ sản xuất không lọc theo ids lâm sàng', () {
      // admin chỉ được cấp RPL02013 nhưng vẫn tra được hồ sơ máy vừa sản xuất.
      expect(admin.canSee('RPL09999'), isFalse); // phạm vi lâm sàng: chặn
      expect(admin.canSeeProduction, isTrue); // phạm vi sản xuất: mở
    });
  });

  group('quản lý tài khoản', () {
    test('root quản lý mọi vai trò', () {
      expect(root.canManageUsers, isTrue);
      for (final r in UserRole.values) {
        expect(root.canManageRole(r), isTrue, reason: r.name);
      }
    });

    test('quản lý sản xuất CHỈ quản lý thao tác viên', () {
      expect(manager.canManageUsers, isTrue);
      expect(manager.canManageRole(UserRole.operator), isTrue);
      for (final r in [UserRole.root, UserRole.admin, UserRole.manager, UserRole.user]) {
        expect(manager.canManageRole(r), isFalse, reason: r.name);
      }
    });

    test('nhân viên, thao tác viên, khách hàng: không', () {
      for (final s in [admin, operator, customer]) {
        expect(s.canManageUsers, isFalse, reason: s.roleCode);
        for (final r in UserRole.values) {
          expect(s.canManageRole(r), isFalse, reason: '${s.roleCode} → ${r.name}');
        }
      }
    });
  });

  group('phạm vi xem máy (lâm sàng) giữ nguyên hành vi cũ', () {
    test('root thấy hết; ids "*" thấy hết; còn lại đúng mã được cấp', () {
      expect(root.canSee('BẤT_KỲ'), isTrue);
      expect(_s(UserRole.admin, ids: ['*']).canSee('RPL09999'), isTrue);
      expect(admin.canSee('rpl02013'), isTrue); // không phân biệt hoa thường
      expect(admin.canSee('RPL02014'), isFalse);
      expect(customer.canSee(''), isFalse);
    });

    test('fromJson tự tính allowAll, không tin backend', () {
      final j = UserSession.fromJson({
        'username': 'x',
        'role': 'manager',
        'ids': ['RPL02013'],
        'allowAll': true, // backend cũ nói dối
      });
      expect(j.role, UserRole.manager);
      expect(j.allowAll, isFalse);
      expect(j.toJson()['role'], 'manager');
    });
  });
}
