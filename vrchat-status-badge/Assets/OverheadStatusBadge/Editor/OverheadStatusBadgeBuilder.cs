#if UNITY_EDITOR
using System.IO;
using UnityEditor;
using UnityEditor.Animations;
using UnityEngine;
using VRC.SDK3.Avatars.Components;
using VRC.SDK3.Avatars.ScriptableObjects;

public static class OverheadStatusBadgeBuilder
{
    private const string ParameterName = "StatusMode";
    private const string LayerName = "Overhead Status";
    private const string RootFolder = "Assets/OverheadStatusBadge";
    private const string GeneratedFolder = RootFolder + "/Generated";

    private static readonly string[] Labels =
    {
        "Hidden",
        "Available",
        "Busy",
        "AFK",
        "Recording",
        "Coding"
    };

    [MenuItem("Tools/Overhead Status Badge/Build On Selected Avatar")]
    public static void BuildOnSelectedAvatar()
    {
        var avatar = Selection.activeGameObject;
        if (avatar == null)
        {
            EditorUtility.DisplayDialog("Overhead Status Badge", "Select your avatar root first.", "OK");
            return;
        }

        var descriptor = avatar.GetComponent<VRCAvatarDescriptor>();
        if (descriptor == null)
        {
            EditorUtility.DisplayDialog("Overhead Status Badge", "Selected object does not have a VRC Avatar Descriptor.", "OK");
            return;
        }

        EnsureFolder(RootFolder);
        EnsureFolder(GeneratedFolder);

        var badgeRoot = CreateBadgeObjects(avatar);
        var controller = EnsureFxController(descriptor);
        ConfigureAnimatorController(controller, avatar, badgeRoot);
        EnsureExpressionParameter(descriptor);
        EnsureExpressionMenu(descriptor);

        AssignFxLayer(descriptor, controller);

        EditorUtility.SetDirty(descriptor);
        AssetDatabase.SaveAssets();
        AssetDatabase.Refresh();

        EditorUtility.DisplayDialog(
            "Overhead Status Badge",
            "Created badge objects, added StatusMode, added the Status expression submenu, and added the Overhead Status FX layer.",
            "OK"
        );
    }

    private static GameObject CreateBadgeObjects(GameObject avatar)
    {
        var head = FindHeadBone(avatar);
        var existing = head.Find("OverheadStatusBadge");
        if (existing != null)
        {
            Object.DestroyImmediate(existing.gameObject);
        }

        var root = new GameObject("OverheadStatusBadge");
        root.transform.SetParent(head, false);
        root.transform.localPosition = new Vector3(0f, 0.42f, 0.02f);
        root.transform.localRotation = Quaternion.identity;
        root.transform.localScale = Vector3.one;

        for (var i = 1; i < Labels.Length; i++)
        {
            var status = new GameObject("Status_" + Labels[i]);
            status.transform.SetParent(root.transform, false);

            var mesh = status.AddComponent<TextMesh>();
            mesh.text = Labels[i];
            mesh.anchor = TextAnchor.MiddleCenter;
            mesh.alignment = TextAlignment.Center;
            mesh.characterSize = 0.055f;
            mesh.fontSize = 72;
            mesh.color = ColorForStatus(i);

            var renderer = status.GetComponent<MeshRenderer>();
            renderer.sortingOrder = 10;

            status.SetActive(false);
        }

        return root;
    }

    private static Transform FindHeadBone(GameObject avatar)
    {
        var animator = avatar.GetComponent<Animator>();
        if (animator != null && animator.isHuman)
        {
            var head = animator.GetBoneTransform(HumanBodyBones.Head);
            if (head != null)
            {
                return head;
            }
        }

        var fallback = FindChildByName(avatar.transform, "Head");
        return fallback != null ? fallback : avatar.transform;
    }

    private static Transform FindChildByName(Transform root, string name)
    {
        foreach (Transform child in root)
        {
            if (child.name == name)
            {
                return child;
            }

            var found = FindChildByName(child, name);
            if (found != null)
            {
                return found;
            }
        }

        return null;
    }

    private static AnimatorController EnsureFxController(VRCAvatarDescriptor descriptor)
    {
        foreach (var layer in descriptor.baseAnimationLayers)
        {
            if (layer.type == VRCAvatarDescriptor.AnimLayerType.FX &&
                layer.animatorController is AnimatorController existing)
            {
                return existing;
            }
        }

        var path = GeneratedFolder + "/OverheadStatus_FX.controller";
        return AssetDatabase.LoadAssetAtPath<AnimatorController>(path) ??
               AnimatorController.CreateAnimatorControllerAtPath(path);
    }

    private static void ConfigureAnimatorController(AnimatorController controller, GameObject avatar, GameObject badgeRoot)
    {
        EnsureAnimatorParameter(controller);
        RemoveLayerIfPresent(controller, LayerName);

        var stateMachine = new AnimatorStateMachine
        {
            name = LayerName
        };
        AssetDatabase.AddObjectToAsset(stateMachine, controller);

        var hidden = stateMachine.AddState("Hidden");
        hidden.motion = CreateClip(avatar, badgeRoot, 0);
        stateMachine.defaultState = hidden;

        for (var i = 1; i < Labels.Length; i++)
        {
            var state = stateMachine.AddState(Labels[i]);
            state.motion = CreateClip(avatar, badgeRoot, i);

            var toState = hidden.AddTransition(state);
            toState.hasExitTime = false;
            toState.duration = 0f;
            toState.AddCondition(AnimatorConditionMode.Equals, i, ParameterName);

            var toHidden = state.AddTransition(hidden);
            toHidden.hasExitTime = false;
            toHidden.duration = 0f;
            toHidden.AddCondition(AnimatorConditionMode.NotEqual, i, ParameterName);
        }

        var layer = new AnimatorControllerLayer
        {
            name = LayerName,
            defaultWeight = 1f,
            stateMachine = stateMachine
        };

        var layers = controller.layers;
        ArrayUtility.Add(ref layers, layer);
        controller.layers = layers;

        EditorUtility.SetDirty(controller);
    }

    private static void EnsureAnimatorParameter(AnimatorController controller)
    {
        foreach (var parameter in controller.parameters)
        {
            if (parameter.name == ParameterName)
            {
                return;
            }
        }

        controller.AddParameter(ParameterName, AnimatorControllerParameterType.Int);
    }

    private static void RemoveLayerIfPresent(AnimatorController controller, string layerName)
    {
        var layers = controller.layers;
        for (var i = layers.Length - 1; i >= 0; i--)
        {
            if (layers[i].name == layerName)
            {
                controller.RemoveLayer(i);
            }
        }
    }

    private static AnimationClip CreateClip(GameObject avatar, GameObject badgeRoot, int activeIndex)
    {
        var clip = new AnimationClip
        {
            name = "Status_" + Labels[activeIndex]
        };

        for (var i = 1; i < Labels.Length; i++)
        {
            var binding = new EditorCurveBinding
            {
                path = AnimationUtility.CalculateTransformPath(badgeRoot.transform.GetChild(i - 1), avatar.transform),
                propertyName = "m_IsActive",
                type = typeof(GameObject)
            };

            var value = activeIndex == i ? 1f : 0f;
            AnimationUtility.SetEditorCurve(clip, binding, AnimationCurve.Constant(0f, 0.01f, value));
        }

        var path = GeneratedFolder + "/" + clip.name + ".anim";
        DeleteAssetIfPresent(path);
        AssetDatabase.CreateAsset(clip, path);
        return clip;
    }

    private static void EnsureExpressionParameter(VRCAvatarDescriptor descriptor)
    {
        var parameters = descriptor.expressionParameters;
        if (parameters == null)
        {
            var path = GeneratedFolder + "/OverheadStatus_Parameters.asset";
            parameters = AssetDatabase.LoadAssetAtPath<VRCExpressionParameters>(path);
            if (parameters == null)
            {
                parameters = ScriptableObject.CreateInstance<VRCExpressionParameters>();
                parameters.parameters = new VRCExpressionParameters.Parameter[0];
                AssetDatabase.CreateAsset(parameters, path);
            }

            descriptor.expressionParameters = parameters;
        }

        foreach (var parameter in parameters.parameters)
        {
            if (parameter.name == ParameterName)
            {
                parameter.valueType = VRCExpressionParameters.ValueType.Int;
                parameter.saved = true;
                parameter.networkSynced = true;
                EditorUtility.SetDirty(parameters);
                return;
            }
        }

        var newParameter = new VRCExpressionParameters.Parameter
        {
            name = ParameterName,
            valueType = VRCExpressionParameters.ValueType.Int,
            defaultValue = 0,
            saved = true,
            networkSynced = true
        };

        var existing = parameters.parameters ?? new VRCExpressionParameters.Parameter[0];
        ArrayUtility.Add(ref existing, newParameter);
        parameters.parameters = existing;
        EditorUtility.SetDirty(parameters);
    }

    private static void EnsureExpressionMenu(VRCAvatarDescriptor descriptor)
    {
        var statusMenu = CreateStatusSubmenu();
        var rootMenu = descriptor.expressionsMenu;
        if (rootMenu == null)
        {
            descriptor.expressionsMenu = statusMenu;
            return;
        }

        for (var i = rootMenu.controls.Count - 1; i >= 0; i--)
        {
            if (rootMenu.controls[i].name == "Status")
            {
                rootMenu.controls.RemoveAt(i);
            }
        }

        if (rootMenu.controls.Count >= 8)
        {
            EditorUtility.DisplayDialog(
                "Overhead Status Badge",
                "Your root expression menu is full. The Status submenu was created, but you need to add it manually or make room in the root menu.",
                "OK"
            );
            return;
        }

        rootMenu.controls.Add(new VRCExpressionsMenu.Control
        {
            name = "Status",
            type = VRCExpressionsMenu.Control.ControlType.SubMenu,
            subMenu = statusMenu
        });

        EditorUtility.SetDirty(rootMenu);
    }

    private static VRCExpressionsMenu CreateStatusSubmenu()
    {
        var path = GeneratedFolder + "/OverheadStatus_StatusSubmenu.asset";
        DeleteAssetIfPresent(path);
        var menu = ScriptableObject.CreateInstance<VRCExpressionsMenu>();

        for (var i = 0; i < Labels.Length; i++)
        {
            menu.controls.Add(new VRCExpressionsMenu.Control
            {
                name = Labels[i],
                type = i == 0
                    ? VRCExpressionsMenu.Control.ControlType.Button
                    : VRCExpressionsMenu.Control.ControlType.Toggle,
                parameter = new VRCExpressionsMenu.Control.Parameter { name = ParameterName },
                value = i
            });
        }

        AssetDatabase.CreateAsset(menu, path);
        return menu;
    }

    private static void AssignFxLayer(VRCAvatarDescriptor descriptor, AnimatorController controller)
    {
        var layers = descriptor.baseAnimationLayers;
        for (var i = 0; i < layers.Length; i++)
        {
            if (layers[i].type != VRCAvatarDescriptor.AnimLayerType.FX)
            {
                continue;
            }

            layers[i].isDefault = false;
            layers[i].animatorController = controller;
            descriptor.baseAnimationLayers = layers;
            return;
        }
    }

    private static Color ColorForStatus(int status)
    {
        switch (status)
        {
            case 1: return new Color(0.25f, 1f, 0.55f);
            case 2: return new Color(1f, 0.35f, 0.35f);
            case 3: return new Color(0.55f, 0.7f, 1f);
            case 4: return new Color(1f, 0.25f, 0.25f);
            case 5: return new Color(0.95f, 0.8f, 0.25f);
            default: return Color.white;
        }
    }

    private static void EnsureFolder(string path)
    {
        if (AssetDatabase.IsValidFolder(path))
        {
            return;
        }

        var parent = Path.GetDirectoryName(path).Replace("\\", "/");
        var folder = Path.GetFileName(path);
        if (!string.IsNullOrEmpty(parent))
        {
            EnsureFolder(parent);
            AssetDatabase.CreateFolder(parent, folder);
        }
    }

    private static void DeleteAssetIfPresent(string path)
    {
        if (AssetDatabase.LoadAssetAtPath<Object>(path) != null)
        {
            AssetDatabase.DeleteAsset(path);
        }
    }
}
#endif
