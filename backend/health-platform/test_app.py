import unittest
from app import bool_value, evaluate, normalize_sensor_data


class AppTests(unittest.TestCase):
    def test_normalize_legacy_payload(self):
        item = normalize_sensor_data({"deviceId": "esp32-1", "heartRate": 78, "bloodOxygen": 98, "temperature": 25.5, "humidity": 46})
        self.assertEqual(item["device_id"], "esp32-1")
        self.assertFalse(item["is_abnormal"])

    def test_normalize_alias_payload(self):
        item = normalize_sensor_data({"heart_Rate": 110, "blood_Oxygen": 94, "temperature": 26, "humidity": 50})
        self.assertTrue(item["is_abnormal"])

    def test_reject_invalid_spo2(self):
        with self.assertRaises(ValueError):
            normalize_sensor_data({"heartRate": 80, "bloodOxygen": 120, "temperature": 26, "humidity": 50})

    def test_expert_warning(self):
        self.assertIn("立即就医", evaluate(75, 88))

    def test_mysql_bit_conversion(self):
        self.assertFalse(bool_value(b"\x00"))
        self.assertTrue(bool_value(b"\x01"))


if __name__ == "__main__":
    unittest.main()
