function doPost(e) {
    try {
      // Parse JSON từ ESP32
      var jsonData = JSON.parse(e.postData.contents);
  
      // Lấy timestamp từ dữ liệu gửi lên để đặt tên sheet
      var sheetName = jsonData.time || "Unnamed_" + new Date().toISOString();
  
      // Lấy file spreadsheet hiện tại
      var ss = SpreadsheetApp.openById("YOUR_SPREADSHEET_ID");
  
      // Kiểm tra nếu sheet đã tồn tại thì xóa (tuỳ ý)
      var existingSheet = ss.getSheetByName(sheetName);
      if (existingSheet) {
        ss.deleteSheet(existingSheet); // Xoá sheet cũ nếu cần
      }
  
      // Tạo sheet mới với tên theo timestamp
      var newSheet = ss.insertSheet(sheetName);
  
      // Ghi tiêu đề vào dòng đầu tiên
      newSheet.appendRow([
        "Device ID",
        "Firmware Version",
        "Time",
        "Amplification Time",
        "Slopes",
        "Origins",
        "LED Power",
        "CT Value",
        "Result",
        "Amplification (raw JSON)",
      ]);
  
      // Ghép dữ liệu để lưu thành hàng
      var row = [
        jsonData.id_device,
        jsonData.version,
        jsonData.time,
        jsonData.amplification_time,
        jsonData.slopes.join(", "),
        jsonData.origins.join(", "),
        jsonData.LED_power.join(", "),
        jsonData.CT_value.join(", "),
        jsonData.result.join(", "),
        JSON.stringify(jsonData.amplification),
      ];
  
      newSheet.appendRow(row);
  
      return ContentService.createTextOutput(
        JSON.stringify({ status: "success", sheet: sheetName })
      ).setMimeType(ContentService.MimeType.JSON);
    } catch (error) {
      return ContentService.createTextOutput(
        JSON.stringify({ status: "error", message: error.message })
      ).setMimeType(ContentService.MimeType.JSON);
    }
  }
  