import io
import unittest
from unittest import mock

import requests

from test.utils import server_base


class ModelRecipeOptionsCleanupTests(unittest.TestCase):
    def test_setup_unload_http_failure_prevents_body(self):
        response = mock.Mock(status_code=200)
        response.json.return_value = {"saved": {"ctx_size": 256}}
        failed_unload = mock.Mock(status_code=503)
        failed_unload.raise_for_status.side_effect = requests.HTTPError(
            "503 Server Error"
        )
        body_entered = False

        with mock.patch.object(
            server_base, "get_model_options", return_value={"saved": {}}
        ), mock.patch.object(
            server_base.requests,
            "post",
            side_effect=[response, failed_unload, response],
        ), mock.patch.object(
            server_base.requests, "delete", return_value=response
        ):
            with self.assertRaisesRegex(requests.HTTPError, "503 Server Error"):
                with server_base.model_recipe_options("TinyLlama"):
                    body_entered = True

        self.assertFalse(body_entered)

    def test_clear_http_failure_surfaces_after_successful_body(self):
        response = mock.Mock(status_code=200)
        response.json.return_value = {"saved": {"ctx_size": 256}}
        failed_clear = mock.Mock(status_code=503)
        failed_clear.raise_for_status.side_effect = requests.HTTPError(
            "503 Server Error"
        )

        with mock.patch.object(
            server_base, "get_model_options", return_value={"saved": {}}
        ), mock.patch.object(
            server_base.requests, "post", return_value=response
        ), mock.patch.object(
            server_base.requests, "delete", return_value=failed_clear
        ), mock.patch(
            "sys.stderr", new_callable=io.StringIO
        ):
            with self.assertRaisesRegex(requests.HTTPError, "503 Server Error"):
                with server_base.model_recipe_options("TinyLlama"):
                    pass

    def test_restore_http_failure_surfaces_after_successful_body(self):
        response = mock.Mock(status_code=200)
        response.json.return_value = {"saved": {"ctx_size": 256}}
        failed_restore = mock.Mock(status_code=503)
        failed_restore.raise_for_status.side_effect = requests.HTTPError(
            "503 Server Error"
        )

        with mock.patch.object(
            server_base,
            "get_model_options",
            return_value={"saved": {"ctx_size": 128}},
        ), mock.patch.object(
            server_base.requests,
            "post",
            side_effect=[response, response, failed_restore, response],
        ), mock.patch.object(
            server_base.requests, "delete", return_value=response
        ), mock.patch(
            "sys.stderr", new_callable=io.StringIO
        ):
            with self.assertRaisesRegex(requests.HTTPError, "503 Server Error"):
                with server_base.model_recipe_options("TinyLlama", ctx_size=256):
                    pass

    def test_unload_http_failure_surfaces_after_successful_body(self):
        response = mock.Mock(status_code=200)
        response.json.return_value = {"saved": {"ctx_size": 256}}
        failed_unload = mock.Mock(status_code=503)
        failed_unload.raise_for_status.side_effect = requests.HTTPError(
            "503 Server Error"
        )

        with mock.patch.object(
            server_base, "get_model_options", return_value={"saved": {}}
        ), mock.patch.object(
            server_base.requests,
            "post",
            side_effect=[response, response, failed_unload],
        ), mock.patch.object(
            server_base.requests, "delete", return_value=response
        ), mock.patch(
            "sys.stderr", new_callable=io.StringIO
        ):
            with self.assertRaisesRegex(requests.HTTPError, "503 Server Error"):
                with server_base.model_recipe_options("TinyLlama"):
                    pass

    def test_cleanup_accepts_already_unloaded_response(self):
        response = mock.Mock(status_code=200)
        response.json.return_value = {"saved": {"ctx_size": 256}}
        already_unloaded = mock.Mock(status_code=404)
        already_unloaded.raise_for_status.side_effect = requests.HTTPError(
            "404 Client Error"
        )

        with mock.patch.object(
            server_base, "get_model_options", return_value={"saved": {}}
        ), mock.patch.object(
            server_base.requests,
            "post",
            side_effect=[response, already_unloaded, already_unloaded],
        ), mock.patch.object(
            server_base.requests, "delete", return_value=response
        ):
            with server_base.model_recipe_options("TinyLlama"):
                pass

    def test_cleanup_transport_failure_surfaces_after_successful_body(self):
        response = mock.Mock(status_code=200)
        response.json.return_value = {"saved": {"ctx_size": 256}}

        with mock.patch.object(
            server_base, "get_model_options", return_value={"saved": {}}
        ), mock.patch.object(
            server_base.requests, "post", return_value=response
        ), mock.patch.object(
            server_base.requests,
            "delete",
            side_effect=requests.ConnectionError("cleanup unavailable"),
        ), mock.patch(
            "sys.stderr", new_callable=io.StringIO
        ) as stderr:
            with self.assertRaisesRegex(
                requests.ConnectionError, "cleanup unavailable"
            ):
                with server_base.model_recipe_options("TinyLlama"):
                    pass

        self.assertIn("cleanup unavailable", stderr.getvalue())

    def test_body_exception_remains_primary_when_cleanup_transport_fails(self):
        response = mock.Mock(status_code=200)
        response.json.return_value = {"saved": {"ctx_size": 256}}

        with mock.patch.object(
            server_base, "get_model_options", return_value={"saved": {}}
        ), mock.patch.object(
            server_base.requests, "post", return_value=response
        ), mock.patch.object(
            server_base.requests,
            "delete",
            side_effect=requests.ConnectionError("cleanup unavailable"),
        ), mock.patch(
            "sys.stderr", new_callable=io.StringIO
        ) as stderr:
            with self.assertRaisesRegex(RuntimeError, "body failed"):
                with server_base.model_recipe_options("TinyLlama"):
                    raise RuntimeError("body failed")

        self.assertIn("cleanup unavailable", stderr.getvalue())

    def test_cleanup_attempts_restore_and_unload_after_clear_failure(self):
        response = mock.Mock(status_code=200)
        response.json.return_value = {"saved": {"ctx_size": 256}}
        state = {"saved": {"ctx_size": 128}, "loaded": True}

        def post(url, json, **_kwargs):
            if url.endswith("/options"):
                state["saved"] = json
            elif url.endswith("/unload"):
                state["loaded"] = False
            return response

        with mock.patch.object(
            server_base,
            "get_model_options",
            return_value={"saved": state["saved"].copy()},
        ), mock.patch.object(
            server_base.requests, "post", side_effect=post
        ), mock.patch.object(
            server_base.requests,
            "delete",
            side_effect=requests.ConnectionError("clear unavailable"),
        ), mock.patch(
            "sys.stderr", new_callable=io.StringIO
        ) as stderr:
            with self.assertRaisesRegex(requests.ConnectionError, "clear unavailable"):
                with server_base.model_recipe_options("TinyLlama", ctx_size=256):
                    state["loaded"] = True

        self.assertEqual(state, {"saved": {"ctx_size": 128}, "loaded": False})
        self.assertIn("clear unavailable", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
