"""
The pytest for cpp class e2sar::LBManager token management methods.

Tests the newly added token management functionality including:
- createToken()
- listTokenPermissions()
- listChildTokens()
- revokeToken()

To make sure it's working, either append the path of "e2sar_py.*.so" to sys.path. E.g,
# import sys
# sys.path.append(
#     '<my_e2sar_build_path>/build/src/pybind')

Or, add this path to PYTHONPATH, e.g,
# export PYTHONPATH=<my_e2sar_build_path>/build/src/pybind

Run with: pytest -m cp -v test_CPToken.py
"""

import pytest
import os

# Make sure the compiled module is added to your path
import e2sar_py

lbm = e2sar_py.ControlPlane.LBManager
ej_uri = e2sar_py.EjfatURI
TokenPermission = e2sar_py.ControlPlane.TokenPermission
TokenDetails = e2sar_py.ControlPlane.TokenDetails

# URI string - should have admin token in EJFAT_URI environment variable
# or use a test URI string here
URI_STR = os.getenv(
    "EJFAT_URI",
    "ejfats://udplbd@192.168.0.3:18347/lb/1?data=127.0.0.1&sync=192.168.88.199:1234"
)


@pytest.mark.cp
def test_create_and_revoke_token_default_permissions():
    """
    Test case (a): Create a new instance token with default permissions
    and immediately revoke it, verifying success of both operations.
    """
    # Create URI with admin token
    uri = ej_uri(URI_STR, ej_uri.TokenType.admin)
    assert isinstance(uri, ej_uri), "EjfatURI creation failed!"

    # Create LBManager instance
    lb_manager = lbm(uri, False)  # False = skip server certificate validation
    assert isinstance(lb_manager, lbm), "LBManager creation failed!"

    # Create a new token with default permissions (read-only on all resources)
    token_name = "test_token_default_perms"
    default_perms = [
        TokenPermission(
            ej_uri.TokenType.admin,
            "",  # empty resource ID = wildcard
            ej_uri.TokenPermission._read_only_
        )
    ]

    result_create = lb_manager.create_token(token_name, default_perms)
    assert not result_create.has_error(), f"Token creation failed: {result_create.error().message() if result_create.has_error() else ''}"
    assert result_create.has_value(), "Token creation returned no value!"

    created_token = result_create.value()
    assert isinstance(created_token, str), "Created token is not a string!"
    assert len(created_token) > 0, "Created token is empty!"
    print(f"Created token: {created_token[:20]}... (length: {len(created_token)})")

    # Revoke the token using the token string
    result_revoke = lb_manager.revoke_token_by_string(created_token)
    assert not result_revoke.has_error(), f"Token revocation failed: {result_revoke.error().message() if result_revoke.has_error() else ''}"
    assert result_revoke.has_value(), "Token revocation returned no value!"
    assert result_revoke.value() == 0, f"Token revocation returned non-zero: {result_revoke.value()}"

    print(f"Successfully created and revoked token: {token_name}")


@pytest.mark.cp
def test_create_list_validate_revoke_token_with_permissions():
    """
    Test case (b): Create a new instance token with reserve and update permissions,
    call listTokenPermissions() and validate the created token and its permissions,
    delete the token, verifying success of each operation.
    """
    # Create URI with admin token
    uri = ej_uri(URI_STR, ej_uri.TokenType.admin)
    assert isinstance(uri, ej_uri), "EjfatURI creation failed!"

    # Create LBManager instance
    lb_manager = lbm(uri, False)
    assert isinstance(lb_manager, lbm), "LBManager creation failed!"

    # Create a new token with reserve and update permissions
    token_name = "test_token_reserve_update"
    permissions = [
        TokenPermission(
            ej_uri.TokenType.instance,
            "",  # empty resource ID = wildcard for all instances
            ej_uri.TokenPermission._reserve_
        ),
        TokenPermission(
            ej_uri.TokenType.instance,
            "",
            ej_uri.TokenPermission._update_
        )
    ]

    # Create the token
    result_create = lb_manager.create_token(token_name, permissions)
    assert not result_create.has_error(), f"Token creation failed: {result_create.error().message() if result_create.has_error() else ''}"
    assert result_create.has_value(), "Token creation returned no value!"

    created_token = result_create.value()
    assert isinstance(created_token, str), "Created token is not a string!"
    assert len(created_token) > 0, "Created token is empty!"
    print(f"Created token with reserve/update perms: {created_token[:20]}...")

    # List token permissions using the token string
    result_list = lb_manager.list_token_permissions_by_string(created_token)
    assert not result_list.has_error(), f"List permissions failed: {result_list.error().message() if result_list.has_error() else ''}"
    assert result_list.has_value(), "List permissions returned no value!"

    token_details = result_list.value()
    assert isinstance(token_details, TokenDetails), "Token details is not TokenDetails type!"

    # Validate the token details
    assert token_details.name == token_name, f"Token name mismatch: expected '{token_name}', got '{token_details.name}'"
    assert len(token_details.permissions) == 2, f"Expected 2 permissions, got {len(token_details.permissions)}"
    assert token_details.id > 0, f"Token ID should be positive, got {token_details.id}"
    assert len(token_details.created_at) > 0, "Token created_at is empty!"

    print(f"Token details: name={token_details.name}, id={token_details.id}, created_at={token_details.created_at}")
    print(f"Token has {len(token_details.permissions)} permissions:")

    # Validate permissions
    perm_types = []
    for perm in token_details.permissions:
        assert isinstance(perm, TokenPermission), "Permission is not TokenPermission type!"
        perm_types.append(perm.permission)
        print(f"  - Resource Type: {perm.resourceType}, Resource ID: '{perm.resourceId}', Permission: {perm.permission}")

    # Check that we have reserve and update permissions
    assert ej_uri.TokenPermission._reserve_ in perm_types, "Reserve permission not found in token!"
    assert ej_uri.TokenPermission._update_ in perm_types, "Update permission not found in token!"

    # Revoke the token
    result_revoke = lb_manager.revoke_token_by_string(created_token)
    assert not result_revoke.has_error(), f"Token revocation failed: {result_revoke.error().message() if result_revoke.has_error() else ''}"
    assert result_revoke.has_value(), "Token revocation returned no value!"
    assert result_revoke.value() == 0, f"Token revocation returned non-zero: {result_revoke.value()}"

    print(f"Successfully created, validated, and revoked token: {token_name}")


@pytest.mark.cp
def test_create_list_child_tokens_and_revoke():
    """
    Test case (c): Using admin token create two instance tokens,
    call listChildTokens() using admin token and verify the two new tokens
    are its children, revoke both new tokens, verifying success of each operation.
    """
    # Create URI with admin token
    uri = ej_uri(URI_STR, ej_uri.TokenType.admin)
    assert isinstance(uri, ej_uri), "EjfatURI creation failed!"

    # Create LBManager instance
    lb_manager = lbm(uri, False)
    assert isinstance(lb_manager, lbm), "LBManager creation failed!"

    # Get the admin token from the URI
    admin_token_result = uri.get_admin_token()
    assert not admin_token_result.has_error(), "Failed to get admin token from URI!"
    admin_token = admin_token_result.value()
    assert len(admin_token) > 0, "Admin token is empty!"

    # Get initial count of child tokens for baseline
    result_initial_children = lb_manager.list_child_tokens_by_string(admin_token)
    initial_child_count = 0
    if not result_initial_children.has_error():
        initial_child_count = len(result_initial_children.value())
    print(f"Initial child token count: {initial_child_count}")

    # Create first token
    token_name_1 = "test_child_token_1"
    permissions_1 = [
        TokenPermission(
            ej_uri.TokenType.instance,
            "",
            ej_uri.TokenPermission._register_
        )
    ]

    result_create_1 = lb_manager.create_token(token_name_1, permissions_1)
    assert not result_create_1.has_error(), f"First token creation failed: {result_create_1.error().message() if result_create_1.has_error() else ''}"
    created_token_1 = result_create_1.value()
    assert len(created_token_1) > 0, "First created token is empty!"
    print(f"Created first child token: {created_token_1[:20]}...")

    # Create second token
    token_name_2 = "test_child_token_2"
    permissions_2 = [
        TokenPermission(
            ej_uri.TokenType.session,
            "",
            ej_uri.TokenPermission._read_only_
        )
    ]

    result_create_2 = lb_manager.create_token(token_name_2, permissions_2)
    assert not result_create_2.has_error(), f"Second token creation failed: {result_create_2.error().message() if result_create_2.has_error() else ''}"
    created_token_2 = result_create_2.value()
    assert len(created_token_2) > 0, "Second created token is empty!"
    print(f"Created second child token: {created_token_2[:20]}...")

    # List child tokens using admin token
    result_list_children = lb_manager.list_child_tokens_by_string(admin_token)
    assert not result_list_children.has_error(), f"List child tokens failed: {result_list_children.error().message() if result_list_children.has_error() else ''}"
    assert result_list_children.has_value(), "List child tokens returned no value!"

    child_tokens = result_list_children.value()
    assert isinstance(child_tokens, list), "Child tokens is not a list!"
    assert len(child_tokens) >= initial_child_count + 2, f"Expected at least {initial_child_count + 2} child tokens, got {len(child_tokens)}"

    print(f"Total child tokens: {len(child_tokens)}")

    # Verify both new tokens are in the children list
    child_token_names = [child.name for child in child_tokens]
    assert token_name_1 in child_token_names, f"First token '{token_name_1}' not found in child tokens!"
    assert token_name_2 in child_token_names, f"Second token '{token_name_2}' not found in child tokens!"

    print(f"Verified both tokens are children of admin token:")
    for child in child_tokens:
        if child.name in [token_name_1, token_name_2]:
            print(f"  - {child.name} (id={child.id}, created_at={child.created_at})")

    # Revoke first token
    result_revoke_1 = lb_manager.revoke_token_by_string(created_token_1)
    assert not result_revoke_1.has_error(), f"First token revocation failed: {result_revoke_1.error().message() if result_revoke_1.has_error() else ''}"
    assert result_revoke_1.value() == 0, f"First token revocation returned non-zero: {result_revoke_1.value()}"
    print(f"Successfully revoked first token: {token_name_1}")

    # Revoke second token
    result_revoke_2 = lb_manager.revoke_token_by_string(created_token_2)
    assert not result_revoke_2.has_error(), f"Second token revocation failed: {result_revoke_2.error().message() if result_revoke_2.has_error() else ''}"
    assert result_revoke_2.value() == 0, f"Second token revocation returned non-zero: {result_revoke_2.value()}"
    print(f"Successfully revoked second token: {token_name_2}")

    # Verify child tokens are removed
    result_final_children = lb_manager.list_child_tokens_by_string(admin_token)
    if not result_final_children.has_error():
        final_child_tokens = result_final_children.value()
        final_child_names = [child.name for child in final_child_tokens]
        assert token_name_1 not in final_child_names, f"First token '{token_name_1}' still in child list after revocation!"
        assert token_name_2 not in final_child_names, f"Second token '{token_name_2}' still in child list after revocation!"
        print(f"Verified both tokens removed from child list. Final count: {len(final_child_tokens)}")

    print("Successfully created, verified, and revoked two child tokens!")


@pytest.mark.cp
def test_list_token_permissions_by_id():
    """
    Bonus test: Test listing token permissions using token ID instead of string.
    """
    # Create URI with admin token
    uri = ej_uri(URI_STR, ej_uri.TokenType.admin)
    assert isinstance(uri, ej_uri), "EjfatURI creation failed!"

    # Create LBManager instance
    lb_manager = lbm(uri, False)
    assert isinstance(lb_manager, lbm), "LBManager creation failed!"

    # Create a test token
    token_name = "test_token_by_id"
    permissions = [
        TokenPermission(
            ej_uri.TokenType.admin,
            "",
            ej_uri.TokenPermission._read_only_
        )
    ]

    result_create = lb_manager.create_token(token_name, permissions)
    assert not result_create.has_error(), "Token creation failed!"
    created_token = result_create.value()

    # First, get the token details to extract the ID
    result_list_by_string = lb_manager.list_token_permissions_by_string(created_token)
    assert not result_list_by_string.has_error(), "List permissions by string failed!"
    token_details_from_string = result_list_by_string.value()
    token_id = token_details_from_string.id

    print(f"Token ID extracted: {token_id}")

    # Now test listing by ID
    result_list_by_id = lb_manager.list_token_permissions_by_id(token_id)
    assert not result_list_by_id.has_error(), f"List permissions by ID failed: {result_list_by_id.error().message() if result_list_by_id.has_error() else ''}"
    token_details_from_id = result_list_by_id.value()

    # Verify the details match
    assert token_details_from_id.id == token_id, "Token ID mismatch!"
    assert token_details_from_id.name == token_name, "Token name mismatch!"
    assert len(token_details_from_id.permissions) == len(token_details_from_string.permissions), "Permission count mismatch!"

    print(f"Successfully retrieved token details by ID: {token_id}")

    # Clean up - revoke by ID
    result_revoke = lb_manager.revoke_token_by_id(token_id)
    assert not result_revoke.has_error(), "Token revocation by ID failed!"
    assert result_revoke.value() == 0, "Token revocation returned non-zero!"

    print(f"Successfully revoked token by ID: {token_id}")


if __name__ == "__main__":
    pytest.main(["-v", __file__])
